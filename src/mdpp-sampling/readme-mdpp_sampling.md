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

- decode: detect channel multihits and insert some invalid data point
  between the trace of the first hit and the trace of the second hit. Right now
  the trace is just extended by the second hit, without any gap in-between.

- WaveformSinkWidget is mostly a copy/paste of MdppSamplingUi. Factor out the
  common parts.

- Make sinc interpolation optionally use a larger window size.

- Make sinc interpolation also output samples at the start and end before the
  max window size is reached. This way x intervals would be uniform across the
  output values. Rigth now they are not, e.g 0, 1, 2, 2.5, 3, 3.5 for factor=1.

# DONE

- Add 2D Waveforms: similar to H2D plots:
  * y is the channel number / trace number
  * x is the sample number * dtSample
  * z (color value) is the sample value at position x

- Try using util::span<std::pair<double, double>> for trace data everywhere.
  Interpolate could write into a properly sized span, stopping if it runs out of
  space.

- MDPP-32 and MDPP-16 amplitude and time data formats differ. Need to use
  different sets of filters depending on the module type when decoding.

- Create a separate TraceViewerSink and corresponding UI to be able to process
  analysis parameter arrays as sample trace data. The trace history buffer will be
  part of the sink. Keeping the trace history buffers of different channels in the
  same module will be possible and easy to implement as the sink is fed data
  event-by-event.

- Then build a MdppChannelTracePlotData but for util::span. ChannelTrace gets
  cleaned up and stays more mdpp/module specific. Add option to add more traces
  to TracePlotWidget. Pass an option to only draw symbols but no lines. Use this
  to implement drawing symbols at the points of raw waveform data.
