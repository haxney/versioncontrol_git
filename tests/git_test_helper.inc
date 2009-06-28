<?php
// $Id$

/**
 * @file
 * Helper methods for hook test cases.
 *
 * Takes care of the setup and teardown of functionality common to all of the
 * repository hooks.
 *
 * Copyright 2009 by Daniel Hackney ("dhax", http://drupal.org/user/384635)
 */

require_once(drupal_get_path('module', 'versioncontrol_git') .'/gitlib/tests/test_helpers.test');

class GitTestHelper extends DrupalWebTestCase {

  /**
   * Root of the temporary SimpleTest files directory.
   *
   * All temporary files are written here.
   */
  protected $file_path;

  /**
   * The old working directory, before chdir().
   */
  protected $old_wd;

  /**
   * The full path to the root of the Drupal installation.
   */
  protected $base_path;

  /**
   * The OS-level user running the web site.
   */
  protected $server_user;

  /**
   * Array containing information about the repository and working copy.
   */
  protected $repo = array();

  /**
   * Array of hook info. The key is the hook name and the value is an array with
   * the following elements:
   *
   *   - src - The source of the xgit script implementing the hook.
   *   - dst - The destination path of the xgit script.
   *   - file - The path of the git hook which calls the xgit script.
   *   - text - The source of the git hook.
   *
   *   array(
   *     'name' => 'path',
   *   );
   */
  protected $hooks = array();

  /**
   * An array of the hooks provided.
   */
  protected $hook_names = array('update');

  /**
   * Help set up a test to use Subversion.
   */
  function setUp() {
    parent::setUp('versioncontrol', 'versioncontrol_git', 'commitlog');

    $this->old_wd          = getcwd();
    $this->file_path       = realpath(file_directory_path());
    $this->repo['path']    = $this->file_path .'/repo.git';
    $this->repo['xgit']    = $this->repo['path'] .'/hooks/xgit';
    $this->repo['url']     = 'file://'. $this->repo['path'];
    $this->repo['wc_path'] = $this->file_path .'/gitwc';
    $this->repo['created'] = FALSE;
    $this->base_path       = dirname($_SERVER['SCRIPT_FILENAME']);
    $this->obj_regex       = '([0-9a-f]+)';
    $this->module_source   = realpath(drupal_get_path('module', 'versioncontrol_git'));


    // The config 'hook' is special, so fill it in separately.
    $this->hooks['config']['src'] = $this->module_source .'/xgit/xgit-config.php';
    $this->hooks['config']['dst'] = $this->repo['xgit'] .'/xgit-config.php';
    $format = "\$xgit['drupal_path'] = '%s';\n".
              "\$GLOBALS['xgit_debug'] = TRUE;\n".
              "\$GLOBALS['simpletest_db_prefix'] = '%s';";

    $this->hooks['config']['text'] = sprintf($format, $this->base_path, $GLOBALS['db_prefix']);

    require_once($this->hooks['config']['src']);
    require_once($this->module_source . 'gitlib/git.php');

    // Fill in each of the hooks.
    foreach ($this->hook_names as $hook) {
      $this->_fillHook($hook);
    }
  }

  /**
   * Fills in the information array for a single hook.
   *
   * @param $hook
   *   The name of the hook for which to fill in the info.
   */
  protected function _fillHook($hook) {
    $this->hooks[$hook] = array(
      'file' => sprintf('%s/hooks/%s', $this->repo['path'], $hook),
      'src'      => sprintf('%s/xgit/xgit-%s.php', $this->module_source, $hook),
      'dst'      => sprintf('%s/xgit-%s.php', $this->repo['xgit'], $hook),
    );

    $dst = $this->hooks[$hook]['dst'];
    $config = $this->hooks['config']['dst'];
    $format = "#!/bin/sh\n".
              // Comment out this line to turn off Xdebug.
              "export XDEBUG_CONFIG='remote_port=9001'\n".
              'php %s %s $@';
    $this->hooks[$hook]['text'] = sprintf($format, $dst, $config);

    return;
  }

  /**
   * Return to old working directory and clean up directories.
   */
  function tearDown() {
    chdir($this->old_wd);
    parent::tearDown();
  }

  /**
   * Create the subversion repository.
   */
  function initRepo() {
    $cmd = 'GIT_DIR="%s" git init --bare';
    shell_exec(sprintf($cmd, $this->repo['path']));
    mkdir($this->repo['xgit']);
    mkdir($this->repo['wc_path']);

    copy($this->hooks['config']['src'], $this->hooks['config']['dst']);

    // Append simpletest-specific info to the config file.
    $file = fopen($this->hooks['config']['dst'], 'a');
    fwrite($file, $this->hooks['config']['text']);
    fclose($file);
    $this->repo['created'] = TRUE;
    return;
  }

  /**
   * Copy the named hook to the repository directory. Throws an exception if the
   * repository is not already initialized.
   *
   * @param $hook
   *   The name of the hook to initialize.
   *
   * @return
   *   Null.
   */
  function initHook($hook) {
    if (!$this->repo['created']) {
      throw new Exception(t('Repository has not yet been created.'));
    }

    // Copy the xgit files
    copy($this->hooks[$hook]['src'], $this->hooks[$hook]['dst']);

    $file = fopen($this->hooks[$hook]['file'], 'w');
    fwrite($file, $this->hooks[$hook]['text']);
    fclose($file);
    chmod($this->hooks[$hook]['file'], 0755);

    return;
  }

  /**
   * Initialize the repository in the Drupal database.
   *
   * @param $update_method
   *   The method to use to update the repository.
   *
   * @return
   *   The repository object.
   */
  function initDrupalRepo($name, $update_method=VERSIONCONTROL_GIT_UPDATE_CRON) {
    $menu_path = 'admin/project/versioncontrol-repositories/add-git';
    $this->repo['name'] = $name;

    $edit = array();
    $edit['repo_name'] = $this->repo['name'];
    $edit['root'] = $this->repo['url'];
    $edit['update_method'] = $update_method;
    $this->drupalPost($menu_path, $edit, t('Save repository'));
    $repo = array_shift(versioncontrol_get_repositories(array(
                'names' => array($this->repo['name'])
              )));
    $this->repo = array_merge($this->repo, $repo);

    // Append the repo id to the xgit-config file.
    $file = fopen($this->hooks['config']['dst'], 'a');
    fprintf($file, "\$xgit['repo_id'] = '%s';\n", $this->repo['repo_id']);
    fclose($file);
    return $repo;
  }

  /**
   * Create an GIT account for the user with the given uid.
   */
  function initGitAccount($uid, $username) {
    $menu_path = 'versioncontrol/register/'. $uid .'/'. $this->repo['id'];
    $edit = array();
    $edit['account_name'] = $username;

    $this->drupalPost($menu_path, $edit, t('Create Git account'));
  }

  /**
   * Check out a working copy of the repository.
   *
   * This should be verified to work in a test.
   */
  function wcClone() {
    chdir($this->file_path);
    $format = 'git clone %s %s';
    return shell_exec(sprintf($format, $this->repo['url'], $this->repo['wc_path']));
  }

  /**
   * Adds a sample file to the working copy.
   *
   * Does not perform any checks for consistency.
   *
   * @param $path
   *   The path (relative to the root of the working copy) of the file to create
   *   and add.
   *
   * @param $contents
   *   The contents of the sample file.
   *
   * @return
   *   The output of "git add $path"
   */
  function wcAddSampleFile($path='source.txt', $contents='file contents') {
    chdir($this->repo['wc_path']);
    $file = fopen($path, 'w');
    fwrite($file, $contents);
    fclose($file);

    return shell_exec("git add $path");
  }

  /**
   * Run 'git commit' with the message given.
   *
   * @param $msg
   *   The commit message.
   */
  function wcCommit($msg = 'commit message') {
    return shell_exec("git commit -m '$msg' 2>&1");
  }

  /**
   * Push to the default (origin) remote branch and return the result.
   */
  function wcPush() {
    exec('git push origin master', $output, $ret_val);

    return array(implode("\n", $output), $ret_val);
  }

  /**
   * Make sure the given operation appears in the commit log on the current
   * page.
   *
   * @param $operation
   *   The operation for which to check.
   */
  function assertOperationLog($op_id, $operation) {
    $options = array('query' => array('id' => $op_id));
    $this->drupalGet('commitlog', $options);

    $this->assertResponse(
      200,
      t('User can access commit log.')
    );

    $this->assertNoText(
      t('No log messages found.'),
      t('Make sure a log message is found.'),
      t('operation log')
    );

    $this->assertText(
      $operation['message'],
      t('Make sure page message matches operation message.'),
      t('operation log')
    );

    $theme_string = theme('commitlog_operations_page', array(
              'vc_op_ids' => array($op_id)
            ));

    // For some reason, the closing tag of the outer <div> appears on the same
    // line in $page, but not in the content of the page. It doesn't seem like
    // that should happen. Convert both to XML and then cast them to strings.
    $theme_xml = new SimpleXMLElement($theme_string);
    $returned = array_shift($this->elements->xpath('//div[@class="commitlog"]'));

    $this->assertEqual(
      (string) $returned,
      (string) $theme_xml,
      t('Found a themed page with the given commit.'),
      t('operation log')
    );
  }

  /**
   * Run the given hook with a variable number of arguments.
   *
   * @param $hook
   *   The name of the hook to run.
   *
   * @param ...
   *   Variable number of arguments to pass to the hook.
   *
   * @return
   *   A list of the output of the command and the return value.
   */
  function execHook($hook) {
    $args = func_get_args();
    $command = $this->hooks[array_shift($args)]['file'] .' ';
    $command .= implode(' ', $args);
    exec($command, $output, $retval);

    $output = implode("\n", $output);
    return array($output, $retval);
  }
}
