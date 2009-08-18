<?php
// $Id$

/**
 * @file
 * Set of helper methods for testing gitlib functionality.
 *
 * A number of useful helpers for testing gitlib.
 *
 * Copyright 2009 by Daniel Hackney ("dhax", http://drupal.org/user/384635)
 */

/**
 * A collection of methods to help with testing.
 */
abstract class GitLibTestBase extends DrupalWebTestCase {
  /**
   * Helper to check for the creation of a certain number of objects as a result
   * of a fast-import.
   *
   * @param $stderr
   *   The stderr output from GitFastImport::fastImport().
   *
   * @param $type
   *   The type of object to check. One of 'blobs', 'trees', 'commits', 'tags',
   *   'marks'.
   *
   * @param $counts
   *   The expected number of objects as an array. The form is:
   *
   *     array(
   *       'created' => 1,
   *       'duplicates' => 0,
   *     )
   *
   *   If $type is set to 'marks', then the expected format is:
   *
   *     array(
   *       'created' => 1024,
   *       'unique' => 1
   *     )
   *
   *   If an option is omitted, it will not be checked.
   *
   * @param $group
   *   The group to set for the SimpleTest assertion.
   */
  function assertObjectStats($stderr, $type, $counts, $group) {
    $pattern = '/ {6}%s {0,3}: +(?<created>\d+) \( +(?<dup>\d+) (?:duplicates|unique)/';
    $pattern = sprintf($pattern, preg_quote($type));
    $this->assertTrue(
      preg_match($pattern, $stderr, $matches),
      t('Type found in statistics.'),
      $group
    );
    $created = intval($matches['created']);
    $duplicates = $unique = intval($matches['dup']);

    if (isset($counts['created'])) {
      $this->assertIdentical(
        $created,
        intval($counts['created']),
        t('Number of created objects matches expectation.'),
        $group
      );
    }

    switch ($type) {
      case 'blobs':
      case 'trees':
      case 'commits':
      case 'tags':
        if (isset($counts['duplicates'])) {
          $this->assertIdentical(
            $duplicates,
            intval($counts['duplicates']),
            t('Number of duplicate objects matches expectation.'),
            $group
          );
        }
      break;
      case 'marks':
        if (isset($counts['unique'])) {
          $this->assertIdentical(
            $unique,
            intval($counts['unique']),
            t('Number of unique marks matches expectation.'),
            $group
          );
        }
      break;
      default:
        $this->fail(
          t('Wrong type given to assertObjectStats().'),
          $group
        );
    }
  }

    /**
   * Perform an assertIdentical(), but only if $expected is not NULL.
   */
  function assertIfSet($actual, $expected, $message, $group) {
    if (isset($expected)) {
      $this->assertIdentical(
        $actual,
        $expected,
        t($message. ' matches expected.'),
        $group
      );
    }
  }


  /**
   * Helper to check for the existence and value of a particular mark pointing
   * to a blob.
   *
   * @param $marks
   *   The marks return value from GitFastImport::fastImport().
   *
   * @param $mark_id
   *   The id number of the mark to find.
   *
   * @param $mark_data
   *   The expected data of the mark. This must exactly match the data contained
   *   in the mark for the assertion to pass.
   *
   * @param $group
   *   The group to set for the SimpleTest assertion.
   */
  function assertMarkBlob($marks, $mark_id, $mark_data, $group) {
    $this->assertNotNull(
      $marks[$mark_id],
      t('Mark exists in output.'),
      $group
    );

    $type = shell_exec(sprintf('GIT_DIR=%s git cat-file -t %s',
              escapeshellarg($this->repo_dir),
              $marks[$mark_id]));
    $this->assertTrue(
      is_int(strpos($type, 'blob')),
      t('Mark is of type "blob".'),
      $group
    );

    $data = shell_exec(sprintf('GIT_DIR=%s git cat-file blob %s',
              escapeshellarg($this->repo_dir),
              $marks[$mark_id]));
    $this->assertIdentical(
      $data,
      $mark_data,
      t('Expected data matches recorded mark'),
      $group
    );
  }

    /**
   * Helper to check for the existence and value of a particular mark pointing
   * to a commit. Does not completely check all aspects of a commit, but just
   * enough to ensure that the commit is there and is consistent.
   *
   * @param $marks
   *   The marks output parameter from GitFastImport::fastImport().
   *
   * @param $mark_id
   *   The id of the mark to check.
   *
   * @param $mark_data
   *   An array containing data about the commit. Any array elements not
   *   included will not be checked. The array is of the form:
   *
   *   array(
   *     'tree' => 'SHA-1',
   *     'author' => array(
   *       'name' => "name",
   *       'email' => "name@example.com",
   *       'when' => array(
   *         'time' => 1245480094,
   *         'offutc' => '+0000',
   *       ),
   *     ),
   *     'committer' => array(
   *       'name' => "name",
   *       'email' => "name@example.com",
   *       'when' => array(
   *         'time' => 1245480094,
   *         'offutc' => '+0000',
   *       ),
   *     ),
   *     'message' => 'commit message',
   *   )
   *
   * @param $group
   *   The group to set for the SimpleTest assertion.
   */
  function assertMarkCommit($marks, $mark_id, $mark_data, $group) {
    $this->assertNotNull(
      $marks[$mark_id],
      t('Mark exists in output.'),
      $group
    );

    $type = shell_exec(sprintf('GIT_DIR=%s git cat-file -t %s',
              escapeshellarg($this->repo_dir),
              $marks[$mark_id]));
    $this->assertTrue(
      is_int(strpos($type, 'commit')),
      t('Mark is of type "commit".'),
      $group
    );

    $data = shell_exec(sprintf('GIT_DIR=%s git cat-file commit %s',
              escapeshellarg($this->repo_dir),
              $marks[$mark_id]));

    // Ack, aughh, hurk! Matches the output of 'git cat-file commit <SHA-1>'
    $pattern = "/tree (?<tree>[0-9a-f]{40})\n".

               "author (?<author_name>.+) <(?<author_email>.+)> ".
               "(?<author_date>\d+) (?<author_tz>[-+]\d{4})\n".

               "committer (?<committer_name>.+) <(?<committer_email>.+)> ".
               "(?<committer_date>\d+) (?<committer_tz>[-+]\d{4})\n".

               "\n(?<message>.*)/";
    $this->assertTrue(
      preg_match($pattern, $data, $matches),
      t('Commit message is printed in the correct format.'),
      $group
    );
    $tree = $matches['tree'];
    $author_name = $matches['author_name'];
    $author_email = $matches['author_email'];
    $author_date = intval($matches['author_date']);
    $author_tz = $matches['author_tz'];
    $committer_name = $matches['committer_name'];
    $committer_email = $matches['committer_email'];
    $committer_date = intval($matches['committer_date']);
    $committer_tz = $matches['committer_tz'];
    $commit_message = $matches['message'];

    // Check each of the attributes.
    $this->assertIfSet($tree, $mark_data['tree'], 'Tree SHA-1', $group);
    $this->assertIfSet($author_name, $mark_data['author']['name'], 'Author name', $group);
    $this->assertIfSet($author_email, $mark_data['author']['email'], 'Author email', $group);
    $this->assertIfSet($author_date, $mark_data['author']['when']['time'], 'Author date', $group);
    $this->assertIfSet($author_tz, $mark_data['author']['when']['offutc'], 'Author timezone', $group);
    $this->assertIfSet($committer_name, $mark_data['committer']['name'], 'Committer name', $group);
    $this->assertIfSet($committer_email, $mark_data['committer']['email'], 'Committer email', $group);
    $this->assertIfSet($committer_date, $mark_data['committer']['when']['time'], 'Committer date', $group);
    $this->assertIfSet($committer_tz, $mark_data['committer']['when']['offutc'], 'Committer timezone', $group);
    $this->assertIfSet($commit_message, $mark_data['message'], 'Commit message', $group);
  }

    /**
   * Helper for asserting the success of a fast-import execution.
   *
   * @param $retval
   *   The return value from fastImport().
   *
   * @param $group
   *   The group to set for the SimpleTest assertion.
   *
   * @return
   *   None.
   */
  function assertCommandSuccess($retval, $group) {
    $this->assertEqual(
      $retval,
      0,
      t('Command completes successfully.'),
      $group
    );
  }

  /**
   * Helper for asserting that no marks were created in the execution of
   * fast-import.
   *
   * @param $marks
   *   The $marks return value from fastImport().
   *
   * @param $group
   *   The group to set for the SimpleTest assertion.
   *
   * @return
   *   None.
   */
  function assertNoMarks($marks, $group) {
    $this->assertFalse(
      $marks,
      t('Command produced no marks.'),
      $group
    );
  }

    /**
   * Helper for asserting that the command produced statistics (which will
   * happen on a successful run).
   *
   * @param $stderr
   *   The stderr result from GitFastImport::fastImport().
   *
   * @param $group
   *   The group to set for the SimpleTest assertion.
   *
   * @return
   *   None.
   */
  function assertStatisticsExists($stderr, $group) {
    $this->assertTrue(
      is_int(strpos($stderr, 'git-fast-import statistics:')),
      t('Command produces statistics.'),
      $group
    );
  }
}

// ------------ lixlpixel recursive PHP functions -------------
// recursive_remove_directory( directory to delete, empty )
// expects path to directory and optional TRUE / FALSE to empty
// ------------------------------------------------------------
function recursive_remove_directory($directory, $empty=FALSE) {
  if(substr($directory,-1) == '/') {
    $directory = substr($directory,0,-1);
  }
  if(!file_exists($directory) || !is_dir($directory)) {
    return FALSE;
  } elseif(is_readable($directory)) {
    $handle = opendir($directory);
    while (FALSE !== ($item = readdir($handle))) {
      if($item != '.' && $item != '..') {
        $path = $directory.'/'.$item;
        if(is_dir($path)) {
          recursive_remove_directory($path);
        } else {
          unlink($path);
        }
      }
    }
    closedir($handle);
    if($empty == FALSE) {
      if(!rmdir($directory)) {
        return FALSE;
      }
    }
  }
  return TRUE;
}
