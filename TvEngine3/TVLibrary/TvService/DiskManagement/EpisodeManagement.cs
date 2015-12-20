#region Copyright (C) 2005-2011 Team MediaPortal

// Copyright (C) 2005-2011 Team MediaPortal
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
using System.Linq;
using TvDatabase;
using TvLibrary.Log;

namespace TvService
{
  public class EpisodeManagement
  {
    public List<Recording> GetEpisodes(string title, IList<Recording> recordings)
    {
      List<Recording> episodes = new List<Recording>();
      foreach (Recording recording in recordings)
      {
        if (String.Compare(title, recording.Title, true) == 0)
        {
          episodes.Add(recording);
        }
      }
      return episodes;
    }

    public Recording GetOldestEpisode(List<Recording> episodes)
    {
      Recording oldestEpisode = null;
      DateTime oldestDateTime = DateTime.MaxValue;
      foreach (Recording rec in episodes)
      {
        if (rec.StartTime < oldestDateTime)
        {
          oldestDateTime = rec.StartTime;
          oldestEpisode = rec;
        }
      }
      return oldestEpisode;
    }

    #region episode disk management

    public void OnScheduleEnded(string recordingFilename, Schedule recording, TvDatabase.Program program)
    {
      Schedule parentrec = recording.ReferencedSchedule();
      Log.Write("diskmanagement: recording {0} / {1} ended. type:{2} max episodes:{3} Endtime: {4}",
                program.Title, recording.ProgramName, (ScheduleRecordingType)recording.ScheduleType, recording.MaxAirings, recording.EndTime.ToString());
      if (parentrec != null)
      {
        Log.Write("diskmanagement: parent recording was {0} type:{1} S{2}E{3}",
                  parentrec.ProgramName, (ScheduleRecordingType)parentrec.ScheduleType,
                  program.SeriesNumAsInt, program.EpisodeNumAsInt);
        // Update only series and episodedata for EveryTimeOnEveryChannelOnlyNewerEpisodes and only if the recording is complete
        if ((ScheduleRecordingType)parentrec.ScheduleType == ScheduleRecordingType.EveryTimeOnEveryChannelOnlyNewerEpisodes &&
             DateTime.Now >= recording.EndTime)
        {
          Log.Write("diskmanagement: Update series and episodedata for {0} type:{1} to: S{2}E{3}",
                    parentrec.ProgramName, (ScheduleRecordingType)parentrec.ScheduleType,
                    program.SeriesNumAsInt, program.EpisodeNumAsInt);

          parentrec.LastseriesNum = program.SeriesNumAsInt;
          parentrec.LastepisodeNum = program.EpisodeNumAsInt;
          parentrec.Persist();

        }
      }

      CheckEpsiodesForRecording(recording, program);
    }

    private void CheckEpsiodesForRecording(Schedule schedule, TvDatabase.Program program)
    {
      if (!schedule.DoesUseEpisodeManagement)
        return;

      //check how many episodes we got
      IList<Recording> recordings = Recording.ListAll()
        .Where(r => String.Compare(program.Title, r.Title, StringComparison.OrdinalIgnoreCase) == 0)
        .OrderBy(r => r.StartTime).ToList();

      for (int i = 0; i < recordings.Count - schedule.MaxAirings; i++)
      {
        Recording oldestEpisode = recordings[i];

        // Delete the file from disk and the recording entry from the database.
        bool result = RecordingFileHandler.DeleteRecordingOnDisk(oldestEpisode.FileName);
        if (result)
        {
          oldestEpisode.Delete();
        }
        Log.Write("diskmanagement:   Delete episode {0} {1} {2} {3} {4}",
          oldestEpisode.ReferencedChannel(),
          oldestEpisode.Title,
          oldestEpisode.StartTime.ToLongDateString(),
          oldestEpisode.StartTime.ToLongTimeString(),
          result ? "succeeded" : "failed!");
      }
    }

    #endregion
  }
}