<?php
// $Id$

/**
 * @file
 * Provides access checking for 'git push' commands.
 *
 * Run once for each reference being updated by an invocation of 'git push' to a
 * remote repository. A exit code of zero allows the update to occur, while any
 * non-zero exit code will prevent the update from occurring.
 *
 * Copyright 2009 by Daniel Hackney ('chrono325', http://drupal.org/user/384635)
 */

define('VERSIONCONTROL_GIT_ERROR_WRONG_ARGC', 1);
define('VERSIONCONTROL_GIT_ERROR_NO_CONFIG', 2);

/**
 * Prints usage of this program.
 */
function xgit_help($cli, $output_stream=STDERR) {
  fwrite($output_stream, "Usage: php $cli <config file> <ref> <oldrev> <newrev>\n\n");
}

function xgit_help_hook($cli, $output_stream=STDERR) {
  fwrite($output_stream, "$cli: GIT_DIR must be set.\n\n");
}

/**
 * The main function of the hook.
 *
 * Expects the following arguments:
 *
 *   - $argv[1] - The path of the configuration file, xgit-config.php.
 *   - $argv[2] - The name of the ref being stored.
 *   - $argv[3] - The old object name stored in the ref.
 *   - $argv[4] - The new objectname to be stored in the ref.
 *
 * @param argc
 *   The number of arguments on the command line.
 *
 * @param argv
 *   Array of the arguments.
 */
function xgit_init($argc, $argv) {
  $this_file = array_shift($argv);   // argv[0]

  if ($argc != 5) {
    xgit_help($this_file, STDERR);
    exit(VERSIONCONTROL_GIT_ERROR_WRONG_ARGC);
  }

  $config_file = array_shift($argv); // argv[1]
  $ref         = array_shift($argv); // argv[2]
  $old_obj     = array_shift($argv); // argv[3]
  $new_obj     = array_shift($argv); // argv[4]

  // Load the configuration file and bootstrap Drupal.
  if (!file_exists($config_file)) {
    fwrite(STDERR, t('Error: failed to load configuration file.') ."\n");
    exit(VERSIONCONTROL_GIT_ERROR_NO_CONFIG);
  }
  include_once $config_file;

  // Admins and other privileged users don't need to go through any checks.
  if (!in_array($username, $xgit['allowed_users'])) {
    // Do a full Drupal bootstrap.
    xgit_bootstrap($xgit);

    $ref_type = xgit_ref_type($ref);
    if ($ref_type === FALSE) {
      fwrite(STDERR, "Given reference '$ref' is invalid.\n\n");
      exit(VERSIONCONTROL_GIT_ERROR_INVALID_REF);
    }
    _xgit_assert_type(array(
        $new_obj => array('commit', 'tag'),
        $old_obj => array('commit', 'tag'),
      ));

    $label = xgit_label_for($ref, $old_obj, $new_obj);
    foreach (xgit_get_commits($old_obj, $new_obj) as $commit) {
      $access = xgit_check_commit_access($commit, $label);

      // Fail and print out error messages if commit access has been denied.
      if (!$access) {
        fwrite(STDERR, implode("\n\n", versioncontrol_get_access_errors()) ."\n\n");
        exit(VERSIONCONTROL_GIT_ERROR_NO_ACCESS);
      }
    }
  }

  // Everything succeeded. Allow operation to complete.
  exit(0);
}

xgit_init($argc, $argv);
