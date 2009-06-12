<?php
// $Id$

/**
 * @file
 * Configuration variables and bootstrapping code for all Git hook scripts.
 *
 * Copyright 2008 by Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 * Copyright 2009 by Daniel Hackney ('chrono325', http://drupal.org/user/384635)
 */

// ------------------------------------------------------------
// Required customization
// ------------------------------------------------------------

// Base path of drupal directory (no trailing slash)
$xgit['drupal_path'] = '';

// Drupal repository id that this installation of scripts is going to
// interact with. In order to find out the repository id, go to the
// "VCS repositories" administration page, then click on the "edit" link of
// the concerned repository, and notice the final number in the resulting URL.
$xgit['repo_id'] = -1;

// ------------------------------------------------------------
// Optional customization
// ------------------------------------------------------------

// These users are always allowed full access, even if we can't
// connect to the DB. This optional list should contain the Git
// usernames (not the Drupal username if they're different).
$xgit['allowed_users'] = array();

// If you run a multisite installation, specify the directory
// name that your settings.php file resides in (ex: www.example.com)
// If you use the default settings.php file, leave this blank.
$xgit['multisite_directory'] = '';

// ------------------------------------------------------------
// Access control
// ------------------------------------------------------------

// Boolean to specify if users should be allowed to delete tags (= branches).
$xgit['allow_tag_removal'] = TRUE;

// Error message for the above permission.
$xgit['tag_delete_denied_message'] = <<<EOF
** ERROR: You are not allowed to delete tags.

EOF;

// ------------------------------------------------------------
// Shared code
// ------------------------------------------------------------

// Error constants.
define('VERSIONCONTROL_GIT_ERROR_WRONG_ARGC',      1);
define('VERSIONCONTROL_GIT_ERROR_NO_CONFIG',       2);
define('VERSIONCONTROL_GIT_ERROR_NO_ACCOUNT',      3);
define('VERSIONCONTROL_GIT_ERROR_NO_GIT_DIR',      4);
define('VERSIONCONTROL_GIT_ERROR_INVALID_REF',     5);
define('VERSIONCONTROL_GIT_ERROR_UNEXPECTED_TYPE', 6);
define('VERSIONCONTROL_GIT_ERROR_NO_ACCESS',       7);

// An empty sha1 sum, represents the parent of the initial commit or the
// deletion of a reference.
define('VERSIONCONTROL_GIT_EMPTY_REF', '0000000000000000000000000000000000000000');

function xgit_bootstrap($xgit) {

  // Add $drupal_path to current value of the PHP include_path.
  set_include_path(get_include_path() . PATH_SEPARATOR . $xgit['drupal_path']);

  chdir($xgit['drupal_path']);

  // Bootstrap Drupal so we can use drupal functions to access the databases, etc.
  if (!file_exists('./includes/bootstrap.inc')) {
    fwrite(STDERR, "Error: failed to load Drupal's bootstrap.inc file.\n");
    exit(1);
  }

  // Set up the multisite directory if necessary.
  if ($xgit['multisite_directory']) {
    $_SERVER['HTTP_HOST'] = $xgit['multisite_directory'];
    // Set a dummy script name, so the multisite configuration
    // file search will always trigger.
    $_SERVER['SCRIPT_NAME'] = '/foo';
  }

  require_once './includes/bootstrap.inc';
  drupal_bootstrap(DRUPAL_BOOTSTRAP_FULL);

  require_once(drupal_get_path('module', 'versioncontrol_git') .'/versioncontrol_git.module');
  require_once(drupal_get_path('module', 'versioncontrol_git') .'/versioncontrol_git.log.inc');
  $xsvn['repo'] = versioncontrol_get_repository($xsvn['repo_id']);
}

/**
 * Returns the author of the given commit in the repository.
 *
 * @param $commit
 *   The commit ID for which to find the author.
 *
 * @return
 *   The author of the commit or FALSE if none is found.
 */
function xgit_get_commit_author($commit) {
  $lines = _xgit_load_commit($commit);

  $author = FALSE;
  // Walk the lines until the first "Author:" line is found.
  foreach ($lines as $line) {
    if (preg_match('/^(?:Author|Tagger)\:/ (.+)', $line, $matches) === 0) {
      $author = trim($matches[1]);
      break;
    }
    // Headers end with a blank line; prevent any other information from being
    // mistakenly recognized as an author line.
    if (strlen($line) === 0) {
      break;
    }
  }

  return $author;
}

/**
 * Returns the files and directories which were modified by the commit with
 * their status.
 *
 * The possible statuses are:
 *
 *   (A) Added
 *   (C###) Copied (### percent similar)
 *   (D) Deleted
 *   (M) Modified
 *   (R###) Renamed (### percent similar)
 *   (T) Have their type (i.e. regular file, symlink, submodule, ...) changed
 *   (U) Are unmerged
 *   (X) Are unknown
 *   (B) Have had their pairing broken
 *
 * Taken from the git-log(1) manpage.
 *
 * @param $commit
 *   The commit ID for which to find the modified files.
 *
 * @return
 *    An array of files and directories modified by the commit or
 *    transaction. The keys are the paths of the file and the value is an array
 *    with the following structure:
 *
 *      - 'status' - Always present. Is one of the statuses described
 *                   above. Note that the 'copied' and 'renamed' statuses have
 *                   their numbers stripped.
 *
 *      - 'old_path' - Only present for 'copied' and 'renamed' statuses. The
 *                     name of the path from which the current path was copied
 *                     or renamed, respectively.
 *
 */
function xgit_get_commit_files($commit) {
  $lines = _xgit_load_commit($commit);

  $items = array();
  // Walk the array backwards until no more files are found.
  $line = end($lines);
  do {
    // A blank lines marks the end of the commit message, so stop there.
    if (empty($line)) {
      break;
    }

    // Move and copy operations operate on two files.
    list($status, $old_path, $new_path) = split("/\t/", $line);

    if (isset($new_path)) {
      // Only take the first character, as we can't do anything with the
      // similarity match.
      $status = substr($status, 1, 1);
      $items[$new_path] = array(
        'status' => $status,
        'old_path' => $old_path,
      );
    }
    $items[$old_path] = array('status' => $status);
  } while($line = prev($lines));

  return array_reverse($items, TRUE);
}

/**
 * Load and cache a commit from the git repository.
 *
 * @param $object
 *   The object id to load.
 *
 * @param $repo
 *   The path of the repository in which to look.
 *
 * @return

 *   An array containing the lines of looking up the object from the
 *   repository. The form depends on the type of object. If it is a commit or an
 *   un-annotated tag, it looks like this:
 *
 *     commit <commit id>
 *     Author: <author name and email>
 *     Commit: <committer name and email>
 *     <empty line>
 *         <commit message>
 *     <empty line>
 *     <status>\t<modified file 1>
 *     <status>\t<modified file 2>
 *     <...>
 *
 *    Note that if the commit message contains empty lines, they will be
 *    prefixed with four spaces, like the rest of the commit message.
 *
 *    Annotated tags, on the other hand, look like this:
 *
 *      tag <tag name without "refs/tags/">
 *      Tagger: <tagger name and email>
 *      Date: <date in iso8601 format>
 *      <tag message>
 *      <referenced object, usually a commit>
 *
 *    Note that tags do not have to identify commits, and there is no way (with
 *    only this function) to differentiate between a tag pointing at a commit
 *    and a tag pointing at a file whose first line is "commit <sha1 sum>".
 */
function _xgit_load_object($object) {
  $type = xgit_get_type($object);
  $allowed_types = array(
    'commit',
    'tag',
  );
  if (!in_array($type, $allowed_types)) {
    throw new Exception("Expected object '$object' to be a commit or tag, is type '$type' instead.");
  }

  if (!isset($xgit['objects'][$object]['log'])) {
    $command = 'git show --name-status --pretty=short --date=iso8601 %s';
    $command = sprintf($command, escapeshellarg($object));
    $result = trim(shell_exec($command));
    $result = preg_split('/\n/', $result, -1, PREG_SPLIT_NO_EMPTY);
    $xgit['objects'][$object]['log'] = $result;
  }

  return $xgit['objects'][$object]['log'];
}

/**
 * Check whether the given object is a valid name and exists within the
 * repository.
 *
 * @param $object
 *   The object to check.
 *
 * @return
 *   TRUE if the object is valid, FALSE otherwise.
 */
function xgit_is_valid($object) {
  if (!isset($xgit['objects'][$object]['valid'])) {
    $command = 'git cat-file -t %s';
    $command = sprintf($command, escapeshellarg($object));
    $type = exec($command, $empty, $ret_val);
    $xgit['objects'][$object]['valid'] = $ret_val === 0;
    $xgit['objects'][$object]['type'] = trim($type);
  }

  return $xgit['objects'][$object]['valid'];
}

/**
 * Returns the type of the object as a string.
 *
 * @param $object
 *   The object whose type to get.
 *
 * @return
 *   The type of the object. One of
 *
 *   - blob
 *   - tree
 *   - commit
 *   - tag
 *   - empty
 */
function xgit_get_type($object) {
  if ($object != VERSIONCONTROL_GIT_EMPTY_REF && !xgit_is_valid($object)) {
    throw new Exception("Object '$object' is not valid in this repository");
  }
  if (!isset($xgit['objects'][$object]['type'])) {

    if ($object == VERSIONCONTROL_GIT_EMPTY_REF) {
      $xgit['objects'][$object]['type'] = 'empty';
    } else {
      $command = 'git cat-file -t %s';
      $command = sprintf($command, escapeshellarg($object));
      $xgit['objects'][$object]['type'] = trim(shell_exec($command));
    }
    $xgit['objects'][$object]['valid'] = TRUE;
  }

  return $xgit['objects'][$object]['type'];
}

/**
 * Return the type of the given reference string.
 *
 * @param $ref
 *   The reference string to check.
 *
 * @return
 *   A string representing the type of the reference, or FALSE if an invalid
 *   string. The type returned is one of:
 *
 *     - tags
 *     - heads
 *     - remotes
 */
function xgit_ref_type($ref) {
  switch ($ref) {
    case (strpos($ref, 'refs/tags') === 0):
      $ret = 'tags';
      break;
    case (strpos($ref, 'refs/heads') === 0):
      $ret = 'heads';
      break;
    case (strpos($ref, 'refs/remotes') === 0):
      $ret = 'remotes';
      break;
    default:
      $ret = FALSE;
      break;
  }
  return $ret;
}

/**
 * Determine whether the action is a creation, modification, move, copy, merge
 * or deletion by checking the old reference.
 *
 * TODO: Implement copied and moved detection.
 *
 * @param $old_obj
 *   The old reference to check against the empty ref.
 *
 * @param $new_obj
 *   The new value of this object.
 *
 * @param $branch_or_tag
 *   Treat the input as a branch or tag, meaning only CREATED, MODIFIED, or
 *   DELETED can be returned. Defaults to FALSE.
 *
 * @return
 *   One of the VERSIONCONTROL_ACTION_* constants.
 */
function xgit_action($old_obj, $new_obj, $branch_or_tag=FALSE) {
  switch (TRUE) {
    case ($old_obj === VERSIONCONTROL_GIT_EMPTY_REF):
      return VERSIONCONTROL_ACTION_CREATED;
      break;
    case ($new_obj === VERSIONCONTROL_GIT_EMPTY_REF):
      return VERSIONCONTROL_ACTION_DELETED;
      break;
    case (xgit_merge_info($new_obj) and !$branch_or_tag):
      return VERSIONCONTROL_ACTION_MERGED;
      break;
    default:
      return VERSIONCONTROL_ACTION_MODIFIED;
  }
}

/**
 * Detect whether the given object is a commit merging multiple other commits.
 *
 * @param $object
 *   The object to check.
 *
 * @return
 *   An array of merged references if this is a merge, or FALSE if not.
 */
function xgit_merge_info($object) {
  if(!isset($xgit['objects'][$object]['merge'])) {
    $lines = _xgit_load_object($object);
    $merge = FALSE;
    foreach ($lines as $line) {
      if (preg_match('/^Merge\:/ (.+)', $line, $matches) === 0) {
        $merge = preg_split('/\s+/', trim($matches[1]));
        $merge = preg_replace('/\.+/', '', $merge);
        break;
      }

      // Headers end with an empty line; prevent any non-header information from
      // being mistaken as a merge specification.
      if (strlen($line) === 0) {
        break;
      }
    }

    $xgit['objects'][$object]['merge'] = $merge;
  }

  return $xgit['objects'][$object]['merge'];
}


/**
 * Create a label array for the reference, old, and new objects and return it.
 *
 * @param $ref
 *   The reference name for which to generate the label.
 *
 * @param $old_obj
 *   The old value of the reference.
 *
 * @param $new_obj
 *   The new value of the reference.
 */
function xgit_label_for($ref, $old_obj, $new_obj) {
  $label = array();
  // TODO: should we shorten the ref name, i.e. 'refs/heads/master' => 'master'?
  $label['name'] = $ref;
  $label['type'] = xgit_operation_type($ref);
  $label['action'] = xgit_action($old_obj, $new_obj, TRUE);

  return $label;
}

/**
 *  Return the operation type associated with the given reference.
 *
 *  @param $ref
 *    The reference of which to compute the operation type.
 *
 *  @return
 *    Either VERSIONCONTROL_OPERATION_BRANCH or VERSIONCONTROL_OPERATION_TAG.
 */
function xgit_operation_type($ref) {
   switch (xgit_ref_type($ref)) {
    case 'heads':
    case 'remotes':
      return VERSIONCONTROL_OPERATION_BRANCH;
      break;
    case 'tags':
      return VERSIONCONTROL_OPERATION_TAG;
      break;
     default:
       throw new Exception("Unexpected operation type '$ref' received.");
       break;
  }
}

/**
 * Fill an item array suitable for versioncontrol_has_write_access from an
 * item's path and status.
 *
 * @param $path
 *   The path of the item.
 *
 * @param $properties
 *   An array of properties, in the same format as the return value from
 *   xgit_get_commit_files().
 */
function xgit_get_operation_item($path, $properties) {
  $item['path'] = $path;
  $item['type'] = VERSIONCONTROL_ITEM_FILE;

  switch ($properties['status']) {
    case 'A': // Added
      $item['action'] = VERSIONCONTROL_ACTION_ADDED;
      break;
    case 'C': // Copied
      $item['action'] = VERSIONCONTROL_ACTION_COPIED;
      break;
    case 'D': // Deleted
      $item['action'] = VERSIONCONTROL_ACTION_DELETED;
      $item['type'] VERSIONCONTROL_ITEM_FILE_DELETED;
      break;
    case 'M': // Modified
      $item['action'] = VERSIONCONTROL_ACTION_MODIFIED;
      break;
    case 'R': // Renamed
      $item['action'] = VERSIONCONTROL_ACTION_MOVED;
      break;
    case 'T': // Have their type (i.e. regular file, symlink, submodule, ...) changed
      $item['action'] = VERSIONCONTROL_ACTION_OTHER;
      break;
    case 'U': // Are unmerged
      $item['action'] = VERSIONCONTROL_ACTION_OTHER;
      break;
    case 'X': // Are unknown
      $item['action'] = VERSIONCONTROL_ACTION_OTHER;
      break;
    case 'B': // Have had their pairing broken
      $item['action'] = VERSIONCONTROL_ACTION_OTHER;
      break;
  }
  return $item;
}
