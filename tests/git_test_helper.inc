<?php
// $Id$

/**
 * @file
 * Helper methods for hook test cases.
 *
 * Takes care of the setup and teardown of functionality common to all of the
 * repository hooks.
 *
 * Copyright 2009 by Daniel Hackney ("chrono325", http://drupal.org/user/384635)
 */

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

  /**
   * Run 'git fast-import' on the working copy with the given options.
   *
   * @param $commands

   *   An array of commands, each of which is a command as described in
   *   git-fast-import(1).
   *   The possible commands are:
   *
   *   - commit: Creates a new branch or updates an existing branch by creating
   *             a new commit and updating the branch to point at the newly
   *             created commit.
   *
   *   - tag: Creates an annotated tag object from an existing commit or
   *          branch. Lightweight tags are not supported by this command, as
   *          they are not recommended for recording meaningful points in time.
   *
   *   - reset: Reset an existing branch (or a new branch) to a specific
   *            revision. This command must be used to change a branch to a
   *            specific revision without making a commit on it.
   *
   *   - checkpoint: Forces fast-import to close the current packfile, generate
   *                 its unique SHA-1 checksum and index, and start a new
   *                 packfile. This command is optional and is not needed to
   *                 perform an import.
   *
   *   - progress: Causes fast-import to echo the entire line to its own
   *               standard output. This command is optional and is not needed
   *               to perform an import.
   *
   *   The following are examples of each command, in the same order as they are
   *   given above.
   *
   *   - array(
   *       'command' => 'commit',
   *       'ref' => 'ref/heads/master',
   *       'mark' => 1, // Optional. Any positive integer.
   *       'author' => array(
   *         'name' => "name",
   *         'email' => "name@example.com",
   *         'when' => array(
   *           'time' => 1245480094,
   *           'offutc' => '+0000', // The offset of the timezone from UTC
   *         ),
   *       ),
   *       'committer' => array(
   *         'name' => "name",
   *         'email' => "name@example.com",
   *         'when' => array(
   *           'time' => 1245480094,
   *           'offutc' => '+0000', // The offset of the timezone from UTC
   *         ),
   *       ),
   *       'message' => "commit message",
   *       'from' => 'commitish', // Optional
   *       // Zero or more
   *       'merge' => array(
   *         'commitish1',
   *         'commitish2',
   *       ),
   *
   *       'operations' => array(
   *         array(
   *           'operation' => 'filemodify',
   *           'mode' => 'normal', // 'executable', 'symlink', 'gitlink'
   *           'data' => 'These are the contents of the file', // Raw contents of the file.
   *           'path' => 'path/to/file.txt',
   *         ),
   *
   *         array(
   *           'operation' => 'filedelete',
   *           'path' => 'path/to/delete.txt',
   *         ),
   *
   *         array(
   *           'operation' => 'filecopy',
   *           'src' => 'path/to/source.txt',
   *           'dst' => 'path/to/destination.txt',
   *         ),
   *
   *         array(
   *           'operation' => 'filerename',
   *           'src' => 'path/to/source.txt',
   *           'dst' => 'path/to/destination.txt',
   *         ),
   *
   *         'filedeleteall',
   *       ),
   *     )
   *
   *   - array(
   *       'command' => 'tag',
   *       'name' => 'refs/tags/name'
   *       'from' => 'commitish',
   *       'tagger' => array(
   *         'name' => 'name',
   *         'email' => 'name@example.com',
   *         'when' => array(
   *           'time' => 1245480094,
   *           'offutc' => '+0000',
   *         ),
   *       ),
   *       'message' => 'Tag message.',
   *     )
   *
   *   - array(
   *       'command' => 'reset',
   *       'ref' => 'refs/heads/refname',
   *       'from' => 'commitish', // Optional
   *     )
   *
   *   - array(
   *       'command' => 'checkpoint',
   *     )
   *
   *   - array(
   *       'command' => 'progress',
   *       'message' => 'Progress message',
   *     )
   *
   * @param $options
   *   Command-line options to pass to git fast-import.
   */
  function fastImport($commands, $options) {
    $command = 'git fast-import ';

    $marks_file = $this->file_path . $this->randomName();
    $default_options = array(
      'date-format' => 'raw',
      'stats'
      'export-marks' => $marks_file
    );
    $options = array_merge($default_options, $options);

    // Set the options for git fast-import
    foreach ($options as $key => $value) {
      // If the format is 'key' => 'value', output '--key=value', otherwise, output '--value'
      if (is_string($key)) {
        $command .= sprintf('--%s=%s', $key, $value);
      } else {
        $command .= sprintf('--%s', $value);
      }
    }

    // The file descriptor array for the fast-import process. The read and write
    // specifications are from the view of git-fast-import.
    $descriptorspec = array(
      0 => array("pipe", "r"), // stdin
      1 => array("pipe", "w"), // stdout
      2 => array("pipe", "w"), // stderr
    );

    $proc = proc_open($command, $descriptorspec, $pipes);
    if (!is_resource($proc)) {
      throw new Exception('Failed to open git process.');
    }

    $proc_in = $pipes[0];
    $proc_out = $pipes[1];
    $proc_err = $pipes[2];
    foreach ($commands as $command) {
      $cmd_type = $this->checkAndGet($command, 'command');
      $funcname = 'handle_'. $cmd_type .'_command';
      switch ($cmd_type) {
        case 'commit':
        case 'tag':
        case 'reset':
        case 'checkpoint':
        case 'progress':
          fwrite($proc_in, call_user_func(array($this, $funcname), $command));
        break;
        default:
          throw new Exception("Unknown command type: ". $command['command']);
      }
    }

  }

  /**
   * Check for the existence of $key within $command and return the value if it
   * exists. Throw an exception if it does not exist.
   *
   * @param $command
   *   An array (usually a command for fastImport() ) to check.
   *
   * @param $key
   *   The key to check and get from $command.
   *
   * @param $required
   *   Whether or not the parameter is required to exist. Defaults to TRUE.
   *
   * @return
   *   The value of $command['key']
   */
  protected function checkAndGet($command, $key, $required=TRUE) {
    if (!isset($command[$key]) and $required) {
      throw new Exception("Expected key '$key' does not exist within command.");
    }
    return $command[$key];
  }

  /**
   * Check and return the author (or committer or tagger) from the given
   * array. If any of the parameters is invalid, throws an exception.
   *
   * @param $author
   *   An array which should contain author, committer, or tagger information as
   *   described in fastImport.
   *
   * @param $required
   *   Whether or not this author specification is required to exist.
   *
   * @return
   *   An author array which has been checked for correctness.
   */
  protected function checkAndGetAuthor($command, $key, $required=TRUE) {
    $author = $command[$key];
    if (empty($author) and !$required) {
      return array();
    } elseif (empty($author)) {
      throw new Exception('Expected author specification, but none given.');
    }
    $this->checkAndGet($author, 'when', $required);
    $author = array(
      'name'  => $this->checkAndGet($author, 'name');
      'email' => $this->checkAndGet($author, 'email');
      'when'  => array(
        'time'   => $this->checkAndGet($author['when'], 'time'),
        'offutc' => $this->checkAndGet($author['when'], 'offutc'),
      )
    );
  }

  /**
   * Handle a 'commit' command for fastImport().
   *
   * @param $command
   *   The commit command to handle.
   *
   * @return
   *   The string appropriate for 'git-fast-import' produced from the command.
   */
  function handle_commit_command($command) {
    $output    = '';
    $ref       = $this->checkAndGet($command, 'ref');
    $mark      = $this->checkAndGet($command, 'mark', FALSE);
    $author    = $this->checkAndGetAuthor($command, 'author', FALSE);
    $committer = $this->checkAndGetAuthor($command, 'committer');
    $message   = $this->checkAndGet($command, 'message');
    $from      = $this->checkAndGet($command, 'from', FALSE);
    $merge     = $this->checkAndGet($command, 'merge', FALSE);
    $ops       = $this->checkAndGet($command, 'operations', FALSE);

    $output .= sprintf("commit %s\n", $ref);
    if (!empty($mark)) {
      $output .= sprintf("mark :%\n", $mark);
    }
    if (!empty($author)) {
      $output .= $this->handleAuthor('author', $author);
    }
    $output .= $this->handleAuthor('committer', $committer);
    $output .= $this->handleData($message);
    if (!empty($from)) {
      $output .= sprintf("from %s\n", $from);
    }
    foreach ($merge as $id) {
      $output .= sprintf("merge %s\n", $id);
    }
    foreach ($ops as $op) {
      $output .= handleOperation($op);
    }
    // not necessary in newer versions of git.
    $output .= "\n";

    return $output;
  }

  /**
   * Handle a 'tag' operation for an annotated tag.
   */
  function handle_tag_command($command) {
    $output  = '';
    $name    = $this->checkAndGet($command, 'name');
    $from    = $this->checkAndGet($command, 'from');
    $tagger  = $this->checkAndGetAuthor($command, 'tagger');
    $message = $this->checkAndGet($command, 'message');

    $output .= sprintf("tag %s\n", $name);
    $output .= sprintf("from %s\n", $from);
    $output .= $this->handleAuthor('tagger' $tagger);
    $output .= $this->handleData($message);

    return $output;
  }

  /**
   * Handle a 'reset' operation to change the value of a ref.
   *
   * @param $command
   *   A 'reset' command array as described in fastImport().
   *
   * @return
   *   The git-fast-import(1) representation of $command.
   */
  function handle_reset_command($command) {
    $output = '';
    $ref    = $this->checkAndGet($command, 'ref');
    $from   = $this->checkAndGet($command, 'from', FALSE);

    $output .= sprintf("reset %s\n", $ref);
    if ($from) {
      sprintf("from %s\n", $from);
    }
    $output .= "\n";

    return $output;
  }


  /**
   * Handle a 'checkpoint' operation.
   *
   * @param $command
   *   A 'checkpoint' command array as described in fastImport().
   *
   * @return
   *   The git-fast-import(1) representation of $command.
   */
  function handle_checkpoint_command($command) {
    $output = "checkpoint\n";

    return $output;
  }

  /**
   * Handle a 'progress' operation.
   *
   * @param $command
   *   A 'progress' command array as described in fastImport().
   *
   * @return
   *   The git-fast-import(1) representation of $command.
   */
  function handle_progress_command($command) {
    $message = $this->checkAndGet($command, 'message');
    $output = sprintf("progress %s\n", $message);

    return $output;
  }


  /**
   * Return a data command.
   *
   * @param $data
   *   The data to format.
   *
   * @return
   *   The formatted data command, suitable for git fast-import.
   */
  function handleData($data) {
    return sprintf("data %d\n%s\n", strlen($data), $data);
  }

  /**
   * Format and return the fast-import specification for the given author.
   *
   * @param $role_name
   *   The role to put in the output, such as 'author', 'committer', or
   *   'tagger'.
   *
   * @param $author
   *   The author array, of the form:
   *
   *     'tagger' => array(
   *       'name' => 'name',
   *       'email' => 'name@example.com',
   *       'when' => array(
   *         'time' => 1245480094,
   *         'offutc' => '+0000',
   *       ),
   *     )
   *
   * @return
   *   A formatted string, suitable for inclusion in a git-fast-import(1)
   *   command stream.
   */
  function handleAuthor($role_name, $author) {
    $replacements = array(
      $role_name,
      $author['name'],
      $author['email'],
      $author['when']['time'],
      $author['when']['offutc']
    );
    return vsprintf("%s %s <%s> %s %s\n", $replacements);
  }

  /**
   * Handle the given operation and return the string of the operation.
   */
  protected function handleOperation($op) {
    $output = '';
    $op_type = $this->checkAndGet($op, 'operation');
    switch ($op_type) {
      case 'filemodify':
        $mode = $this->convertMode($this->checkAndGet($op, 'mode');
        $output .= sprintf("M %s inline %s\n",
                   $this->checkAndGet($op, 'mode'),
                   $mode);
        $output .= $this->handleData($this->checkAndGet($op, 'data'));
        break;

      case 'filedelete':
        $output .= sprintf("D %s\n", $this->checkAndGet($op, 'path'));
        break;

      case 'filecopy':
        $output .= sprintf("C %s %s\n",
                   $this->checkAndGet($op, 'src'),
                   $this->checkAndGet($op, 'dst'));
        break;

      case 'filerename':
        $output .= sprintf("R %s %s\n",
                   $this->checkAndGet($op, 'src'),
                   $this->checkAndGet($op, 'dst'));
        break;
      case 'filedeleteall':
        $output .= "deleteall\n";
        break;
      default:
        throw new Exception("Unknown operation type '$op_type'.");
    }

    return $output;
  }

  /**
   * Converts a mode string to git's numerical mode specification.
   *
   * @param $mode
   *   The mode string, one of:
   *
   *   - normal
   *   - executable
   *   - symlink
   *   - gitlink
   *
   * @return
   *   The numerical mode, as an integer.
   */
  function convertMode($mode) {
    switch ($mode) {
      case 'normal':
        $mode_int = 100644;
        break;
      case 'executable':
        $mode_int = 100755;
        break;
      case 'symlink':
        $mode_int = 120000;
        break;
      case 'gitlink':
        $mode_int = 160000;
        break;
      default:
        throw new Exception('Mode specification not recognized:'. $this->checkAndGet($op, 'path'));
    }
    return $mode_int;
  }
}
