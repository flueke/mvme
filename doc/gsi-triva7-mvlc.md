- Triva7 receives a LVDS trigger input.
- Triva7 signals VME IRQ 4 in reaction to the trigger input (irq4 is hardcoded
  or at least the standard IRQ for Triva).
- MVLC readout for event0 is started in reaction to IRQ4.
- The event0 readout-stack reads the trigger value from a Triva7 register, masks
  and optionally shifts the value and stores it in the MVLC stack accumulator.
  Then the accumulator is "signaled".
- MVLC needs to be setup with a mapping from Triva7 trigger numbers to MVLC IRQs:

  # Note: triggers 14 and 15 need to be mapped as otherwise Triva7 gets stuck waiting for a response.
  writeabs A32 D16 0xFFFF7000 14  # signal, TRIVA Trigger 14 (DAQ start) mapped to MVLC-IRQ 11  ## NOT used
  writeabs A32 D16 0xFFFF7002 11  # resulting MVLC IRQ. Here start event stack -> dummy event with trigger ID

  writeabs A32 D16 0xFFFF7004 15  # TRIVA Trigger 15 (DAQ stop)
  writeabs A32 D16 0xFFFF7006 11  # resulting MVLC IRQ. Here start event stack -> dummy event with trigger ID

  writeabs A32 D16 0xFFFF7008 1   # Signal, TRIVA trigger 0 mapped to MVLC IRQ11
  writeabs A32 D16 0xFFFF700A 11  # Start event stack

  writeabs A32 D16 0xFFFF700C 0   # not used
  writeabs A32 D16 0xFFFF700E 0

- When the stack accumulator is signaled the MVLC generates the (internal) IRQ
  according to the accu mapping. In the example above the internal IRQ11 is
  used to start the readout for event1 via the Trigger/IO system.
  Event1 reads out the VME modules that are part of the event and then clears
  the Triva7 trigger via a VME write.

- The cycle starts at the beginning with the next Triva7 LVDS trigger input.
