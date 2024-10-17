# Sample Traces / Waveforms in the mvme analysis

## Current State

Starting point was the addition of sampling mode to the MDPP modules at the end
of 2024.  The primary goal was to decode, record and display the incoming sample
data. Next a data source for the analysis was implemented allowing access to
MDPP sample traces as analysis parameter arrays.

### Analysis

DataSourceMdppSampleDecoder implements a fully functioning data source.
Interally decode_mdpp_samples() is called. Max channel and sample counts are
configuration information and thus static during processing.

### UI

MdppSamplingUi can currently be opened from the analysis interface. Is is fed
data across thread boundaries through a Qt signal/slot connection. The producer
is a single instance of MdppSamplingConsumer. It is invoked by MVLC_StreamWorker
in the analysis thread.

In MdppSamplingUi::handleModuleData() (running in the UI thread) incoming data
is decoded, recorded and displayed. During processing the trace history is
dynamically filled, meaning the number of channels and the number of traces per
channel do not have to be known in advance.

A new TracePlotWidget was implemented, new axis scaling and zoom logic is part
of MdppSamplingUi. DecodedMdppSampleEvent can be used to hold traces from
multiple different channels or it can serve as a trace history buffer for a
single channel.

## Roadmap

# TODO

- WaveformSinkWidget is mostly a copy/paste of MdppSamplingUi. Factor out the
  common parts.

- MDPP-32 and MDPP-16 amplitude and time data formats differ. Need to use
  different sets of filters depending on the module type when decoding.

- Try using util::span<std::pair<double, double>> for trace data everywhere.
  Interpolate could write into a properly sized span, stopping if it runs out of
  space.

  Then build a MdppChannelTracePlotData but for util::span. ChannelTrace gets
  cleaned up and stays more mdpp/module specific. Add option to add more traces
  to TracePlotWidget. Pass an option to only draw symbols but no lines. Use this
  to implement drawing symbols at the points of raw waveform data.

  Or make interpolation a feature of the TracePlotWidget and only pass in the
  raw sample data and an InterpolatorSomething. Note sure...

# DONE

Create a separate TraceViewerSink and corresponding UI to be able to process
analysis parameter arrays as sample trace data. The trace history buffer will be
part of the sink. Keeping the trace history buffers of different channels in the
same module will be possible and easy to implement as the sink is fed data
event-by-event.
