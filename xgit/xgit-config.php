<?php
// $Id$

/**
 * @file
 * Configuration variables and bootstrapping code for all Git hook scripts.
 *
 * Copyright 2008 by Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 * Copyright 2009 by Daniel Hackney ('dhax', http://drupal.org/user/384635)
 */

global $xgit;
$xgit = array();
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

// These users are always allowed full access, even if we can't connect to the
// DB. This optional list should contain the Drupal user IDs of the allowed
// users.
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
define('VERSIONCONTROL_GIT_ERROR_WRONG_ARGC',       1);
define('VERSIONCONTROL_GIT_ERROR_NO_CONFIG',        2);
define('VERSIONCONTROL_GIT_ERROR_NO_ACCOUNT',       3);
define('VERSIONCONTROL_GIT_ERROR_NO_GIT_DIR',       4);
define('VERSIONCONTROL_GIT_ERROR_INVALID_REF',      5);
define('VERSIONCONTROL_GIT_ERROR_UNEXPECTED_TYPE',  6);
define('VERSIONCONTROL_GIT_ERROR_NO_ACCESS',        7);
define('VERSIONCONTROL_GIT_ERROR_FAILED_BOOTSTRAP', 8);
define('VERSIONCONTROL_GIT_ERROR_NO_REPOSITORY',    9);
define('VERSIONCONTROL_GIT_ERROR_INVALID_OBJ',      10);
define('VERSIONCONTROL_GIT_ERROR_NO_UID',           11);


// An empty sha1 sum, represents the parent of the initial commit or the
// deletion of a reference.
define('VERSIONCONTROL_GIT_EMPTY_REF', '0000000000000000000000000000000000000000');

function xgit_bootstrap() {
  global $xgit;
  // Add $drupal_path to current value of the PHP include_path.
  set_include_path(get_include_path() . PATH_SEPARATOR . $xgit['drupal_path']);

  if (empty($_ENV['GIT_DRUPAL_UID'])) {
    fwrite(STDERR, "Error: No environment variable 'GIT_DRUPAL_UID' set or it is empty.\n");
    exit(VERSIONCONTROL_GIT_ERROR_NO_UID);
  }
  $xgit['uid'] = $_ENV['GIT_DRUPAL_UID'];

  chdir($xgit['drupal_path']);

  // Bootstrap Drupal so we can use drupal functions to access the databases, etc.
  if (!file_exists('./includes/bootstrap.inc')) {
    fwrite(STDERR, "Error: failed to load Drupal's bootstrap.inc file.\n");
    exit(VERSIONCONTROL_GIT_ERROR_FAILED_BOOTSTRAP);
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
  $xgit['repo'] = versioncontrol_get_repository($xgit['repo_id']);

  if (!isset($_ENV['GIT_DIR'])) {
    xgit_help($this_file, STDERR);
    exit(VERSIONCONTROL_GIT_ERROR_NO_GIT_DIR);
  }

  $xgit['git_dir'] = $_ENV['PWD'];

  if (empty($xgit['repo'])) {
    $message = "Error: git repository with id '%s' does not exist.\n";
    fwrite(STDERR, sprintf($message, $xgit['repo_id']));
    exit(VERSIONCONTROL_GIT_ERROR_NO_REPOSITORY);
  }
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
  global $xgit;
  if (!isset($xgit['objects'][$object]['log'])) {
    $allowed_types = array(
      'commit',
      'tag',
    );
    _xgit_assert_type(array($object, $allowed_types));

    $command = 'git --git-dir="%s" show --name-status --pretty=short --date=iso8601 %s';
    $command = sprintf($command, $xgit['git_dir'], escapeshellarg($object));
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
  global $xgit;
  if (!isset($xgit['objects'][$object]['valid'])) {
    $command = 'git --git-dir="%s" cat-file -t %s';
    $command = sprintf($command, $xgit['git_dir'], escapeshellarg($object));
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
  global $xgit;
  if ($object != VERSIONCONTROL_GIT_EMPTY_REF && !xgit_is_valid($object)) {
    throw new Exception("Object '$object' is not valid in this repository");
  }
  if (!isset($xgit['objects'][$object]['type'])) {

    if ($object == VERSIONCONTROL_GIT_EMPTY_REF) {
      $xgit['objects'][$object]['type'] = 'empty';
    } else {
      $command = 'git --git-dir="%s" cat-file -t %s';
      $command = sprintf($command, $xgit['git_dir'], escapeshellarg($object));
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
  global $xgit;
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
      $item['type'] = VERSIONCONTROL_ITEM_FILE_DELETED;
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

/**
 * Returns an array of commit ids between the old and new commits. For pushes,
 * this will be the list of commits being pushed. It is done using git's
 * "old..new" syntax from git-rev-parse(1).
 *
 * Note that $new is not always a descendant of $old. This will be the case for
 * non-fast forward updates. Users of this function must not rely on that
 * assumption.
 *
 * @param $old
 *   The id of the old commit. This is the old value of the ref before it is
 *   updated.
 *
 * @param $new
 *   The id of the new commit.
 *
 * @param $target_ref
 *   If not NULL, exclude this ref from consideration by 'git rev-list'. This is
 *   used for the 'post-receive' hook, which will need to prevent the updated
 *   ref from being mistaken for a preexisting branch.
 *
 * @return
 *   An array of commit ids. This will necessarily include $new, but will not
 *   include $old.
 */
function xgit_get_commits($old, $new, $target_ref = NULL) {
  global $xgit;
  _xgit_assert_type(array(
      $old => array('commit', 'tag', 'empty'),
      $new => array('commit', 'tag', 'empty'),
    ));

  $range_template = '%s..%s';

  // VERSIONCONTROL_GIT_EMPTY_REF cannot appear as an argument to git rev-list,
  // so if one of the objects is empty, it must be specially dealt with.
  if ($old === VERSIONCONTROL_GIT_EMPTY_REF) {
    // When we are creating a new branch, return all commits which do not exist
    // on any other branch.
    $allrefs = _xgit_all_refs($target_ref);

    $range_template = sprintf('%%2$s --not %s', $allrefs);
  } else if ($new === VERSIONCONTROL_GIT_EMPTY_REF) {
    // A deletion, so no commits are being added.
    return array();
  }

  $range = sprintf($range_template, $old, $new);
  if (!isset($xgit['ranges'][$range])) {
    $command = 'git --git-dir="%s" rev-list %s';
    $command = sprintf($command, $xgit['git_dir'], $range);
    $result = trim(shell_exec($command));
    $result = preg_split('/\n/', $result, -1, PREG_SPLIT_NO_EMPTY);
    $xgit['ranges'][$range] = $result;
  }

  return $xgit['ranges'][$range];
}

/**
 * Return a space-separated string of all of the (local) refs in the git
 * repository.  Optionally exclude a particular ref.
 *
 * @param $except
 *   If set, exclude the named ref from the results.
 *
 * @return
 *   A space-separated string of all ref names in the repository.
 */
function _xgit_all_refs($except = NULL) {
  global $xgit;
  $command = 'git --git-dir="%s" rev-parse --symbolic-full-name --branches --tags';
  $command = sprintf($command, $xgit['git_dir']);
  $result = shell_exec($command);

  $refs = implode(' ', explode("\n", $result));
  foreach ($refs as $key => $value) {
    // Yes, I could put the isset() outside the loop, but I don't want to nest
    // that far.
    if(isset($except) and $value === $except) {
      unset($refs[$key]);
    }
  }
}

/**
 * Gets either an array of branches which are "fully contained by" the given
 * commit or those branches which are not contained by the given commit. This is
 * useful for figuring out which commits are not already contained by a
 * different branch, since
 *
 *   git rev-list <commit> --not <unmerged_branches>
 *
 * will return the commits which are parents of $commit and are not reachable
 * from any other branch. This list can then be used to figure out which commits
 * are introduced by a newly created branch.
 *
 * @param $commit
 *   The commit for which to find the merged branches.
 *
 * @param $merged
 *   Whether to get merged or unmerged branches. If TRUE, returns the merged
 *   branches; FALSE gets the unmerged branches.
 *
 * @param $include_remote
 *   Whether or not to include remote tracking branches in the search. Is FALSE
 *   by default.
 */
function _xgit_get_merged_branches($commit, $merged = TRUE, $include_remote = FALSE) {
  global $xgit;
  _xgit_assert_type(array($commit => 'commit'));

  if (!isset($xgit['objects'][$commit]['unmerged'])) {
    $remote_spec = $include_remote ? '-a' : '';
    $merged_spec = $merged ? '--merged' : '--no-merged';
    $command = 'git --git-dir="%s" branch %s --no-color %s %s';
    $command = sprintf($command, $xgit['git_dir'], $remote_spec, $merged_spec, escapeshellarg($commit));
    $result = shell_exec($command);
    $branches = preg_split("/\n/", $result, -1, PREG_SPLIT_NO_EMPTY);

    // There is a chance that $result may contain a '*' if there is a HEAD and it
    // is included in the output.
    $branches = preg_replace('/^([\* ] )?/', '', $branches);
    $xgit['objects'][$commit]['unmerged'] = $branches;
  }

  return $xgit['objects'][$commit]['unmerged'];
}

/**
 * Check each element of $pairs to make sure that it is the correct type. Throws
 * an exception if it is not.
 *
 * @param $pairs
 *   An array of object ids => expected types (as a string or array). For
 *   example:
 *
 *     array(
 *       '2b49652db10c38589896f552bc6063e8955d664f' => 'commit',
 *       'bb7966298ab644b20f06bf7ea9f35d5f3ce11c45' => 'blob',
 *       '2b49652db10c38589896f552bc6063e8955d664f' => array('commit', 'tag'),
 *     )
 *
 * @return
 *   Nothing, throws exceptions if there is a failure.
 */
function _xgit_assert_type($pairs) {
  $msg = "Type mismatch: expected object '%s' to be type '%s',  received '%s'.";
  foreach($pairs as $object => $type) {
    $received = xgit_get_type($object);

    switch (gettype($type)) {
      case 'string':
        if ($received !== $type) {
          $msg = sprintf($msg, $object, $type, $received);
          throw new Exception($msg);
        }
        break;

      case 'array':
        if (!in_array($received, $type)) {
          $msg = sprintf($msg, $object, implode(', ', $type), $received);
          throw new Exception($msg);
        }
        break;
    }
  }
  return;
}

/**
 * Check the access permissions for an individual commit. Really all this does
 * is convert the commit id and label name to a format suitable for
 * versioncontrol_has_write_access() and passes them off to there.
 *
 * @param $commit
 *   The commit id to check.
 *
 * @param $label
 *   The label array, as specified in versioncontrol_has_write_access().
 *
 * @return
 *   The output of versioncontrol_has_write_access() on the arguments.
 */
function xgit_check_commit_access($commit, $label) {
  global $xgit;
  // Construct basic common array. It will be the same across all cases.
  $operation = array(
    'repo_id' => $xgit['repo_id'],
    'labels' => array(
      array(
        'name' => $ref,
      )),
  );

  $item_paths            = xgit_get_commit_files($commit);
  $operation['type']     = xgit_operation_type($ref);
  $operation['username'] = xgit_get_commit_author($commit);
  $operation['uid']      = $xgit['uid'];
  $operation['labels'][] = $label;

  // Set the $operation_items array from the item path and status.
  foreach ($item_paths as $path => $properties) {
    $item = xgit_get_operation_item($path, $properties);
    $operation_items[$path] = $item;
  }
  return versioncontrol_has_write_access($operation, $operation_items);
}
