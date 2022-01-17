# Splitting

Need a base name and a suffix to form the full filename, e.g. 'my_run' and
'_part':

  my_run<runNumber>_part<NNN>.zip

where NNN is an incrementing 3-digit decimal.

Split based on size or duration.

Make each part a valid listfile, e.g. start with the standard preamble. This
requires the CrateConfig and the mvme VMEConfig data. Could store the full
preamble in a std::string using std::stringstream or some other buffer.

Also need an archive member name for each part. In MVLCReadoutWorker this is
the basename of the listfileArchiveName (make_new_listfile_name()).

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
            close_current_entry();
            close_current_zip_creator();
            open_new_zip_creator();
            open_new_entry();
            write_stored_preamble()
        }

    }
    else if (splitMode_ == SplitByTime)
    {
        auto elapsed = std::chrono::steady_clock::now() - partCreated_;

        if (elapsed >= splitTime_)
        {
            close_current_entry();
            close_current_zip_creator();
            open_new_zip_creator();
            open_new_entry();
            write_stored_preamble()
        }
    }

    return zipCreator_->writeToCurrentEntry(data, size);
}
