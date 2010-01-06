#region Copyright (C) 2005-2010 Team MediaPortal

// Copyright (C) 2005-2010 Team MediaPortal
// http://www.team-mediaportal.com
// 
// MediaPortal is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
// 
// MediaPortal is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with MediaPortal. If not, see <http://www.gnu.org/licenses/>.

#endregion

using System;
using System.Collections.Generic;
using System.IO;
using TvDatabase;
using TvLibrary.Log;

namespace TvService
{
  public class RecordingFileHandler
  {
    /// <summary>
    /// Deletes a recording from the disk where it has been saved.
    /// When recording, a unique filename is generated by concatening a underscore
    /// and number to the file name(for example <title - channel>_1). That unique file
    /// name is stored in the database and should
    /// be used as base when deleting files related to the recording. 
    /// Thus all files with exactly the same base name but with _any_ extension
    /// will be deleted(for example the releated matroska .xml file)
    /// If the above results in an empty folder, it is also removed.
    /// </summary>
    /// <param name="rec">The recording we want to delete the files for.</param>
    public bool DeleteRecordingOnDisk(Recording rec)
    {
      Log.Debug("DeleteRecordingOnDisk: '{0}'", rec.FileName);

      try
      {
        // Check if directory exists first, otherwise GetFiles throws an error
        if (Directory.Exists(Path.GetDirectoryName(rec.FileName)))
        {
          // Find and delete all files with same name(without extension) in the recording dir
          string[] relatedFiles =
            Directory.GetFiles(Path.GetDirectoryName(rec.FileName),
                               Path.GetFileNameWithoutExtension(rec.FileName) + @".*");

          foreach (string fileName in relatedFiles)
          {
            Log.Debug(" - deleting '{0}'", fileName);
            // File.Delete will _not_ throw on "File does not exist"
            File.Delete(fileName);
          }

          CleanRecordingFolders(rec.FileName);
        }
      }
      catch (Exception ex)
      {
        Log.Error("RecordingFileHandler: Error while deleting a recording from disk: {0}", ex.Message);
        return false; // files not deleted, return failure
      }
      return true; // files deleted, return success
    }

    /// <summary>
    /// When deleting a recording we check if the folder the recording
    /// was deleted from can be deleted.
    /// A folder must not be deleted, if there are still files or subfolders in it.
    /// </summary>
    /// <param name="fileName">The recording file which is deleted.</param>
    private static void CleanRecordingFolders(string fileName)
    {
      try
      {
        Log.Debug("RecordingFileHandler: Clean orphan recording dirs for {0}", fileName);
        string recfolder = Path.GetDirectoryName(fileName);
        List<string> recordingPaths = new List<string>();

        IList<Card> cards = Card.ListAll();
        foreach (Card card in cards)
        {
          string currentCardPath = card.RecordingFolder;
          if (!recordingPaths.Contains(currentCardPath))
            recordingPaths.Add(currentCardPath);
        }
        Log.Debug("RecordingFileHandler: Checking {0} path(s) for cleanup", Convert.ToString(recordingPaths.Count));

        foreach (string checkPath in recordingPaths)
        {
          if (checkPath != string.Empty && checkPath != Path.GetPathRoot(checkPath))
          {
            // make sure we're only deleting directories which are "recording dirs" from a tv card
            if (fileName.Contains(checkPath))
            {
              Log.Debug("RecordingFileHandler: Origin for recording {0} found: {1}", Path.GetFileName(fileName),
                        checkPath);
              string deleteDir = recfolder;
              // do not attempt to step higher than the recording base path
              while (deleteDir != Path.GetDirectoryName(checkPath) && deleteDir.Length > checkPath.Length)
              {
                try
                {
                  string[] files = Directory.GetFiles(deleteDir);
                  string[] subdirs = Directory.GetDirectories(deleteDir);
                  if (files.Length == 0)
                  {
                    if (subdirs.Length == 0)
                    {
                      Directory.Delete(deleteDir);
                      Log.Debug("RecordingFileHandler: Deleted empty recording dir - {0}", deleteDir);
                      DirectoryInfo di = Directory.GetParent(deleteDir);
                      if (di != null)
                        deleteDir = di.FullName;
                    }
                    else
                    {
                      Log.Debug(
                        "RecordingFileHandler: Found {0} sub-directory(s) in recording path - not cleaning {1}",
                        Convert.ToString(subdirs.Length), deleteDir);
                      return;
                    }
                  }
                  else
                  {
                    Log.Debug("RecordingFileHandler: Found {0} file(s) in recording path - not cleaning {1}",
                              Convert.ToString(files.Length), deleteDir);
                    return;
                  }
                }
                catch (Exception ex1)
                {
                  Log.Info("RecordingFileHandler: Could not delete directory {0} - {1}", deleteDir, ex1.Message);
                  // bail out to avoid i-loop
                  return;
                }
              }
            }
          }
          else
            Log.Debug("RecordingFileHandler: Path not valid for removal - {1}", checkPath);
        }
      }
      catch (Exception ex)
      {
        Log.Error("RecordingFileHandler: Error cleaning the recording folders - {0},{1}", ex.Message, ex.StackTrace);
      }
    }
  }
}