# MDPP sampling mode

Sampling mode is implemented for mdpp16_scp, mdpp16_qdc, mdpp32_scp and
mdpp32_qdc.

Sampling mode is an extension to the standard data format of MDPP modules.
Amplitudes, integration times, etc are transmitted as usual. Additionally sample
trails for selected channels are output.

**Note**: Modules switch to a new event header format when sampling mode is
active. This header uses 16 instead of 10 bits for the length field. This is
required to accomodate the larger event sizes produced when samples are
transmitted.

## Enabling and configuring sampling mode

Below is the current default mvme VME Script to setup sampling and/or streaming mode.

**Note**: For MDPP32-SCP the maximum number of samples per trace is 500, not 1000!


    # Streaming and Sampling Settings
    # ############################################################################

    #***** Sample output ********
    set sampling 1	   		# 0= no sample output, 1 = sampling.

    #***** Definition of sampling trails ******
    set sample_source 0 		# 0 = from ADC, 1 = deconvoluted, 2 = timing filter, 3 = from shaper.
    set no_offset_correction 0 	# 0 = offset correction; 1= no offset correction
    set no_resampling 0			# 0 = resampling; 1 = no resampling

    set number_presamples 50 # 0 to 1000, number of samples before input edge appears with ADC source
    set number_samples	150  # 2 to 1000, total number of samples required. Even number.

    #********************************************
    #**** From here experts only ****************
    #********************************************

    #***** output data format **********
    set output_format 0 	# 0 = standard WOI format
                            # only for experts:
                            # 4 = compact_streaming,
                            # 8 = standard_streaming
    						# Compact streaming does not support sampling

    #**** Firmware check *************
    read a32 d16 0x600E # firmware revision
    accu_mask_rotate 0x000000ff 32
    accu_test_warn gte 0x50 "MDPP-32_SCP >= FW2050 required for Sampling mode"

    # *** Use the variables defined above ***************

    # output_format register 0x6044
    # 0 (default)   -> window of interest, no samples
    # 4  -> Since SCP FW2050. Enables compact_streaming.
    # 8  -> Since SCP FW2050. Enables standard_streaming.
    # Individual bits: {sampling, standard_streaming, compact_streaming, 0,0}
    #					default: window of interest: standard_streaming = 0, compact_streaming = 0

    0x6044 0x${sampling}${output_format}

    # Sampling parameter (can be set for all channels or per channel pair)
    0x6100 8						# select_chan_pair, 8 = all channels
    0x6146 ${number_presamples}		# number of pre-samples
    0x6148 ${number_samples}		# total samples to output (even number required)

    # sampling source and settings [7:0]
    # bits [1:0]: sample source
    #   00 = from ADC
    #   01 = deconvoluted
    #   10 = timing filter
    #   11 = from shaper
    # settings [7:4]
    #   bit 7 set: no offset correction
    #   bit 6 set: no resampling
    0x614A $(${no_offset_correction}*128+${no_resampling}*64+${sample_source})

Additional info about the registers can be found in the data sheets under **Sample Transmission**:
* https://mesytec.com/products/datasheets/MDPP-32_SCP.pdf
* https://mesytec.com/products/datasheets/MDPP-16_SCP-RCP.pdf

Summary:
* set the desired number of presamples in 0x6146 and the total number of samples in 0x6148.
* write 0x614A to configure the source of the samples and optionally disable
  parts of the processing done in the FPGA (no_offset_correction, no_resampling).
* write 0x6044 to enable/disable sampling and select the data output format.
  This allows to combine streaming and sampling modes.

## Decoding the data

In the module output data the sample trail for a channel follows a channel data
word (amplitude or time).

Each trail begins with a sample header followed by sample words with each word
containing two 14-bit sample values.

### Sample header

| data-sig | 2  | 9           | 9     | 19                     |
|----------|----|-------------|-------|------------------------|
| b00      | 11 | Config word | Phase | Number of sample pairs |

### Sample data

| data-sig | 2  | 14           | 14        |
|----------|----|-------------|------------|
| b00      | 11 |  2nd sample | 1st sample |

Sample values are 14 bit signed integers.

### Possible algorithm

* Read input words until a channel word is found (e.g. amplitude). Record that
  as the current channel number.

* Scan further until the sample signature matches. This is the sample header
  word. Extract config, phase and num_pairs values.

* Extract even and odd sample values from the following words. Repeat until
  either the sample signature does not match anymore or the number of sample
  pairs from the header has been reached (consistency check potential here).

* Repeat until input empty.

* Fillwords (0x0000000), timestamp, extended timestamp and triggerTime words in
  the input data need to be skipped.

### mvme decoder

The source code of the current decoder can be found here:
* https://github.com/flueke/mvme/blob/main/src/mdpp-sampling/mdpp_decode.h
* https://github.com/flueke/mvme/blob/main/src/mdpp-sampling/mdpp_decode.cc

It's planned to extract this into a separate library for easy reuse.

# Decoder and waveform display in the mvme analysis

Right-click an MDPP module and select **New -> MDPP Sample Decoder**. This adds
both the decoder data source and a waveforms display sink to the level0 trees in
the ui.

Decoding can be monitored via right-click on the MDPP Sample Decoder and
selecting **Open Monitor Window**.

The waveforms display allows to see all traces produced by the same readout
event cycle. Alternatively the latest trace data for each channel is buffered
and displayed (**Refresh Mode** in the UI).

**Note**: Requires mvme-1.16.1 or later to work correctly. Switching refresh modes
          in mvme-1.15 was buggy.
