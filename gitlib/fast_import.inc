<?php
// $Id$

/**
 * @file
 * Class for interacting with git-fast-import(1).
 *
 * Simplifies interacting with git-fast-import(1) by allowing the creation of
 * fast-import specifications from within PHP.
 *
 * Copyright 2009 by Daniel Hackney ("dhax", http://drupal.org/user/384635)
 */

/**
 * Helper class for running git-fast-import(1). Can be reused multiple times.
 */
class GitFastImport {

  /**
   * Global default options. Can be overridden by default options passed to the
   * constructor or each individual invocation of fastImport().
   */
  public static $DEFAULT_OPTIONS = array(
    'date-format' => 'raw',
    'stats'
  );

  /**
   * The maximum mark id.
   *
   * According to
   * http://utsl.gen.nz/git-p4raw.html#errata__watch_out_for_gfi_mark___1_048_576
   * if a mark id is above 1024 * 1024 - 1 (= 1,048,575), then the mark
   * specified in the stdin to git fast-import will be completely different
   * (unpredictably so) from the mark exported to the mark file.
   */
  const MARK_ID_MAX = 1048575;

  /**
   * The default options for this instance.
   */
  protected $default_options;

  /**
   * The path to the repository.
   */
  protected $path;

  /**
   * Sets the repository path and default options.
   *
   * @param $path
   *   The path of the repository.
   *
   * @param $default_options
   *   Options use by default for invocations of fastImport().
   */
  public function __construct($path, $default_options = array()) {
    $this->path = $path;
    $this->default_options = array_merge(self::$DEFAULT_OPTIONS, $default_options);
  }

  /**
   * Gets the default options of this instance.
   */
  public function defaultOptions() {
    return $this->default_options;
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
   *       'ref' => 'master',
   *       'mark' => 1, // Optional. Any positive integer.
   *       'author' => array( // Optional.
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
   *       'command' => 'blob',
   *       'mark' => 3, // Any positive number, optional.
   *       'data' => 'the content of the blob',
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
   *   Command-line options to pass to git fast-import. Note that fastImport()
   *   automatically unlinks the file referenced by the 'export-marks' option,
   *   so if the contents of the file are needed, make sure to open it with
   *   fopen() *before* calling fastImport().
   *
   * @return
   *   An array of ($return_value, $marks).
   *
   *   $marks is an array of (markid => SHA-1).
   */
  public function fastImport($commands, $options) {
    $options = array_merge($this->default_options, $options);

    if (empty($options['export-marks'])) {
      $options['export-marks'] = tempnam(sys_get_temp_dir(), 'git-marks');
    }

    $command = $this->buildCommandLine($options);
    // The file descriptor array for the fast-import process. The read and write
    // specifications are from the view of git-fast-import.
    $descriptorspec = array(
      0 => array("pipe", "r"), // stdin
      1 => array("pipe", "w"), // stdout
      2 => array("pipe", "w"), // stderr
    );

    $proc = proc_open($command, $descriptorspec, $pipes, $this->path);
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
        case 'blob':
        case 'checkpoint':
        case 'progress': {
          fwrite($proc_in, call_user_func(array($this, $funcname), $command));
          fflush($proc_in);
        }
        break;
        default:
          throw new Exception("Unknown command type: ". $command['command']);
      }
    }
    fclose($proc_in);
    $stdout_result = stream_get_contents($proc_out);
    $stderr_result = stream_get_contents($proc_err);


    fclose($proc_out);
    fclose($proc_err);
    $ret_val = proc_close($proc);
    $marks = self::readMarksFile($options['export-marks']);

    // Clean up temporary file.
    unlink($options['export-marks']);

    return array($ret_val, $marks, $stdout_result, $stderr_result);
  }

  /**
   * Build the command line string.
   *
   * @param $options
   *   An array of options to pass to git-fast-import(1).
   *
   * @return
   *   A string suitable for execution by proc_open and similar.
   */
  public function buildCommandLine($options=array()) {
    $command = 'git fast-import ';
    // Set the options for git fast-import
    foreach ($options as $key => $value) {
      // If the format is 'key' => 'value', output '--key=value', otherwise, output '--value'
      if (is_string($key)) {
        $command .= sprintf('--%s=%s ', $key, $value);
      } else {
        $command .= sprintf('--%s ', $value);
      }
    }

    return $command;
  }

  /**
   * Read a marks file as output from git-fast-import(1) and return it as an
   * array.
   *
   * @param $filename
   *   The name of the marks file to read.
   *
   * @return
   *   An array of mark-id => SHA-1.
   */
  public static function readMarksFile($filename) {
    $lines = file($filename, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES);
    $marks = array();
    foreach ($lines as $line) {
      $split = preg_split('/ /', $line);

      // Convert the string mark id to the integer value with the leading colon
      // stripped.
      preg_match('/:(?<mark>\d+)/', $split[0], $matches);
      $mark_id = intval($matches['mark']);
      $marks[$mark_id] = $split[1];
    }

    return $marks;
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
    $when = $this->checkAndGet($author, 'when');
    $author = array(
      'name'  => $this->checkAndGet($author, 'name'),
      'email' => $this->checkAndGet($author, 'email'),
      'when'  => array(
        'time'   => $this->checkAndGet($when, 'time'),
        'offutc' => $this->checkAndGet($when, 'offutc'),
      )
    );
    return $author;
  }

  /**
   * Create the output for a 'mark' specification, if it exists. This method is
   * intended to be called with the output of $this->checkAndGet($array, 'mark',
   * FALSE), so if the mark does not actually exist, this command will return
   * the empty string.
   *
   * If $mark is greater than GitFastImport::MARK_ID_MAX, then an exception is
   * thrown, since git's handling of the mark is undefined.
   *
   * @param $mark
   *   Either NULL or a positive integer, which will be the value of the mark
   *   command.
   *
   * @return
   *   Either the empty string (if no mark was given as input) or the
   *   git-fast-import(1) representation of the mark.
   */
  function handleMark($mark) {
    if ($mark > self::MARK_ID_MAX) {
      throw new Exception("Mark value '$mark' is greater than allowed maximum.");
    }
    $output = '';
    if (!empty($mark)) {
      $output .= sprintf("mark :%d\n", $mark);
    }
    return $output;
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
    $output .= $this->handleMark($mark);
    if (!empty($author)) {
      $output .= $this->handleAuthor('author', $author);
    }
    $output .= $this->handleAuthor('committer', $committer);
    $output .= $this->handleData($message);
    if (!empty($from)) {
      $output .= sprintf("from %s\n", $from);
    }

    if ($merge) {
      foreach ($merge as $id) {
        $output .= sprintf("merge %s\n", $id);
      }
    }
    foreach ($ops as $op) {
      $output .= $this->handleOperation($op);
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
    $output .= $this->handleAuthor('tagger', $tagger);
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
    if (!empty($from)) {
      sprintf("from %s\n", $from);
    }
    $output .= "\n";

    return $output;
  }

  /**
   * Handle a 'blob' operation to create a blob.
   *
   * @param $command
   *   A 'blob' command array as described in fastImport().
   *
   * @return
   *   The git-fast-import(1) representation of $command.
   */
  function handle_blob_command($command) {
    $mark = $this->checkAndGet($command, 'mark', FALSE);
    $data = $this->checkAndGet($command, 'data');
    $output = "blob\n";
    $output .= $this->handleMark($mark);
    $output .= $this->handleData($data);
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
        $mode = $this->convertMode($this->checkAndGet($op, 'mode'));
        $output .= sprintf("M %s inline %s\n",
                   $mode,
                   $this->checkAndGet($op, 'path'));
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
