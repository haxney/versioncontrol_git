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
define('VERSIONCONTROL_GIT_ERROR_WRONG_ARGC', 1);
define('VERSIONCONTROL_GIT_ERROR_NO_CONFIG', 2);
define('VERSIONCONTROL_GIT_ERROR_NO_ACCOUNT', 3);
define('VERSIONCONTROL_GIT_ERROR_NO_GIT_DIR', 4);
define('VERSIONCONTROL_GIT_ERROR_INVALID_REF', 4);

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
}

/**
 * Returns the author of the given commit in the repository.
 *
 * @param $rev_
 *   The revision number or transaction ID for which to find the author.
 *
 * @param $is_revision
 *   Is the $rev_or_tx argument a revision number or a transaction identifier?
 *   svnlook needs to know which type of object to look for. True if it is a
 *   revision, false if it is a transaction identifier. Defaults to revision.
 *
 * @param $repo
 *   The repository in which to look for the author.
 */
function xgit_get_commit_author($commit, $repo) {
  $str = _xgit_load_commit($commit, $repo);

  // Trim and return the first line of the string.
  return trim(current(explode("\n", $str)));
}

/**
 * Returns the files and directories which were modified by the commit with
 * their status.
 *
 * The possible statuses are:
 *
 *   (A) Added
 *   (C) Copied
 *   (D) Deleted
 *   (M) Modified
 *   (R) Renamed
 *   (T) Have their type (i.e. regular file, symlink, submodule, ...) changed
 *   (U) Are Unmerged
 *   (X) Are Unknown
 *   (B) Have had their pairing Broken
 *
 * Taken from the git-log(1) manpage.
 *
 * @param $commit
 *   The commit ID for which to find the modified files.
 *
 * @return
 *   An array of files and directories modified by the commit or
 *   transaction. The keys are the paths of the file and the value is the status
 *   of the item, as described above.
 */
function xsvn_get_commit_files($commit) {
  $str = _xgit_load_commit($commit);
  $lines = preg_split('/\n/', $str, -1, PREG_SPLIT_NO_EMPTY);
  // The first line is the author name.
  array_shift($lines);

  // Separate the status from the path names.
  foreach ($lines as $line) {
    // Limit to 2 elements to avoid cutting up paths with spaces.
    list($status, $path) = split('/\s+/', $line, 2);
    $items[$path] = $status;
  }
  return $items;
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
 *   The string result of looking up the object from the repository. The string
 *   will be in the form:
 *
 *     [author name] <[author email]>
 *     [modified file 1]
 *     [modified file 2]
 *     [...]
 */
function _xgit_load_object($object) {
  $type = xgit_get_type($object);
  $allowed_types = array(
    'commit',
    'tag',
  );
  if (!in_array($type, $allowed_types) {
    throw new Exception("Expected object '$object' to be a commit or tag, is type '$type' instead.");
  }

  if (!isset($xgit['objects'][$object]['log'])) {
    $command = 'git show --name-status --pretty=short %s';
    $command = sprintf($command, escapeshellarg($object));
    $xgit['objects'][$object]['log'] = trim(shell_exec($command));
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
