# Splitting

Need a base name and a suffix to form the full filename, e.g. 'my_run' and
'_part':

  my_run<runNumber>_part<NNN>.zip

where NNN is an incrementing 3-digit decimal.

Split based on size or duration.

Make each part a valid listfile, e.g. start with the standard preamble. This
requires the CrateConfig and the mvme VMEConfig data. Could generate the full
preamble once at the start of the DAQ run, store the preamble in a std::string
or std::vector and reuse it for each individual part.

Also need an archive member name for each part. In MVLCReadoutWorker this is
the basename of the listfileArchiveName (make_new_listfile_name()).

Additionally the current analysis config, messages.log and run_notes.txt files
need to be stored in each zip archive upon closing the specific part. This is
annoying as the listfile writer runs in its own thread and does need
thread-safe access to the mentioned data. To handle this introduce a callback
invoked prior to closing a specific listfile part. Note: no archive splitting
must occur when writing these files! The only archive member that should cause
splitting is the listfile itself.

- SplitBySize:

  Remember total bytes written. Check if splitSize bytes where written before every write.
  If the limit would be reached start a new part.

- SplitByTime:

  Start a new part if we are past the split duration. Remember creation time of the new part.

# Required data

* output format (lz4, zip)
* Listfile preamble data (byte stream to write via listfile_write_raw()
* Output filename prefix and archive member name (prefix?)
* Split mode (size, time)
* splitSize and splitTime values

* total bytes written
* part creation time


# Algorithm

// throws std::runtime_error on error
size_t SplitZipWriteHandle::write(const u8 *data, size_t size) override
{
    if (splitMode_ == SplitBySize)
    {
        if (partTotalBytes + size > splitSize_)
        {
            startNextPart();
        }

    }
    else if (splitMode_ == SplitByTime)
    {
        auto elapsed = std::chrono::steady_clock::now() - partCreated_;

        if (elapsed >= splitTime_)
        {
            startNextPart();
        }
    }

    return zipCreator_->writeToCurrentEntry(data, size);
}

void SplitZipWriteHandle::startNextPart()
{
   // invoke additional data callback(s). Each must return an archive member name
   // and a buffer containing the member data.

   close_current_entry();

   for (auto &callback: additionalDataCallbacks)
   {
      memberName, data = callback();
      write_to_new_entry_nosplit(memberName, data);
   }

   close_current_zip_creator();

   open_new_zip_creator();
   open_new_entry();
   write_stored_preamble()
}
