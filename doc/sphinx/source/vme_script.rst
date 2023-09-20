.. index:: VME Script, VMEScript
.. _vme-script-reference:

.. TODO: difference between uploading script to the controller and running them.
.. TODO: where do reads go? how is waiting handled. MVLC does not support waiting!

==================================================
VME Scripts
==================================================

Overview
--------
VME Scripts are plain text files with one command per line. Comments may be
started using the ``#`` character. They extend to the end of the line.
Alternatively blocks can be commented out starting with ``/*`` and ending with
``*/``.

Scripts belonging to a module (**Module Init Scripts**, **VME Interface
Settings**, **Module Reset** and the readout code) will have the **module base
address** added to most of the commands. This allows writing scripts containing
module-relative addresses only. Exceptions are the :ref:`writeabs
<vme-command-writeabs>` and :ref:`readabs <vme-command-readabs>` commands which
do not modify their address argument. The base address can also be temporarily
replaced with a different value by using the :ref:`setbase
<vme-command-setbase>` and :ref:`resetbase <vme-command-resetbase>` commands.

The commands below accept the following values for VME address modes and data
widths:

.. table:: VME Address Modes
  :name: vme-address-modes

  +------------------------------+
  | **Address Mode** (*<amode>*) |
  +==============================+
  | a16                          |
  +------------------------------+
  | a24                          |
  +------------------------------+
  | a32                          |
  +------------------------------+

Additionally raw numeric amode values (e.g. 0x39) are also accepted.

.. only:: html

   |

.. table:: VME Data Widths
  :name: vme-data-widths

  +-----------------------------+
  | **Data Width** (*<dwidth>*) |
  +=============================+
  | d16                         |
  +-----------------------------+
  | d32                         |
  +-----------------------------+

The combination of amode, dwidth and BLT/MBLT/2eSST yields a VME address modifier to be sent over the bus.
Internally these non-privileged (aka user) address modifiers will be used:

.. table:: VME address modifiers used by mvme
  :name: vme-address-modifiers

  +-----------+------------+---------+----------+
  | **amode** | **single** | **BLT** | **MBLT** |
  +===========+============+=========+==========+
  | a16       | 0x29       |         |          |
  +-----------+------------+---------+----------+
  | a24       | 0x39       | 0x3b    |          |
  +-----------+------------+---------+----------+
  | a32       | 0x09       | 0x0b    | 0x08     |
  +-----------+------------+---------+----------+
  | cr        | 0x2f       |         |          |
  +-----------+------------+---------+----------+

Numbers in the script (addresses, transfer counts, masks) may be specified in
decimal, hex or floating point notation using the standard C prefixes (``0x``
for hex). Additionally register values may be written in binary starting with a
prefix of ``0b`` followed by ``0``\ s and ``1``\ s, optionally separated by
``'`` characters.

Example: ``0b1010'0101'1100'0011`` is equal to ``0xa5c3``

CR/CSR addressing is implemented for the MVLC VME controller and recognized by
all commands dealing with address modes, e.g. **read**, **write** and
**mvlc_read_to_accu**.

.. _vme-script-commands:

Commands
--------

.. _vme-command-write:
.. _vme-command-writeabs:

Writing
~~~~~~~
* **write** *<amode> <dwidth> <address> <value>*
* **writeabs** *<amode> <dwidth> <address> <value>*

**writeabs** uses the given *<address>* unmodified, meaning the module base address will not be added.

There is a short syntax version of the write command: if a line consists of only two numbers
separated by whitespace, a write using 32-bit addressing (a32) and 16-bit register width (d16) is
assumed. The address is the first number, the value to be written is the second number.

Example: ``0x6070 3`` is the same as ``write a32 d16 0x6070 3``

.. _vme-command-read:
.. _vme-command-readabs:

Reading
~~~~~~~
* **read** *<amode> <dwidth> <address> ['slow'|'late']*
* **readabs** *<amode> <dwidth> <address> ['slow'|'late']*

Reads a single value from the given *<address>*.

**readabs** uses the given *<address>* unmodified, meaning the module base address will not be added.

The optional last argument 'slow'/'late' is implemented for the MVLC only. It
causes the VME read to be carried out on the trailing edge of the modules DTACK
signal instead of at the leading edge. This is a workaround for VME modules
which do signal DTACK while not having the actual data ready yet.

During manual script execution the value read from the VME bus is stored in the
script local accumulator. The accumulator can be modified and tested using
:ref:`accu_mask_rotate <vme-command-accu-mask-rotate>` and
:ref:`accu_test <vme-command-accu-test>`.

.. _vme-command-blt:
.. _vme-command-bltfifo:
.. _vme-command-mblt:
.. _vme-command-mbltfifo:
.. _vme-command-2esst:
.. _vme-command-2essts:

Block Transfers
~~~~~~~~~~~~~~~
mvme supports the following read-only block transfer commands:

* **blt** *<amode> <address> <count>*
* **bltfifo** *<amode> <address> <count>*
* **mblt** *<amode> <address> <count>*
* **mbltfifo** *<amode> <address> <count>*
* **mblts** *<amode> <address> <count>* (**MVLC only**)
* **mbltsfifo** *<amode> <address> <count>* (**MVLC only**)
* **2esst** *<address> <rate> <count>* (**MVLC only**)
* **2esstfifo** *<address> <rate> <count>* (**MVLC only**)
* **2essts** *<address> <rate> <count>* (**MVLC only**)
* **2esstsfifo** *<address> <rate> <count>* (**MVLC only**)

**blt** and **bltfifo** transfer *<count>* number of 32-bit words, **mblt** and **mbltfifo**
transfer 64-bit words. **2esst** uses the 2eSST protocol for data transfer.

The **\*fifo** variants do not increment the given starting address.

**mblts** stands for *MBLT swapped* and is the same as MBLT but swaps the two
32-bit words in each transferred 64-bit word. It is only supported by the MVLC.

The **2esst** and **2essts** commands accept the following *<rate>* arguments:

.. table:: 2esst rates
  :name: 2esst-rates

  +-----------+------------+-------+
  | 160       | 160mb      | 0     |
  +-----------+------------+-------+
  | 276       | 276mb      | 1     |
  +-----------+------------+-------+
  | 320       | 320mb      | 2     |
  +-----------+------------+-------+

**2essts** is the word swapped version of **2esst** like **mblts** for
MBLT transfers.

.. note::
  For the MVLC there is no difference between the FIFO and non-FIFO block
  reads. FIFO mode only makes a difference if the controller interrupts the
  block transfer after a fixed number of cycles (usually 256) and then starts a
  new block transfer either from the starting address (FIFO) mode or from the
  incremented address (non-FIFO mode). The MVLC performs block transfers
  without interruptions which means the starting address is transmitted only
  once and it is up to the individual module how it handles the block transfer.

Miscellaneous
~~~~~~~~~~~~~

.. _vme-command-wait:

wait
^^^^
* **wait** *<waitspec>*

Delays script execution for the given amount of time. *<waitspec>* is a number followed by one of
``ns``, ``ms`` or ``s`` for nanoseconds, milliseconds and seconds respectively. If no suffix is
given milliseconds are assumed.

.. note::
  The wait command is only available when directly executing a script from
  within mvme. It is not supported in command stacks for the MVLC and SIS3153
  controllers.

  The VMUSB has limited support for the wait command in command stacks with a
  waitspec resolution of **200 ns** and the maximum possible delay being
  **51000 ns**.

Example: ``wait 500ms # Delay script execution for 500ms``

.. _vme-command-marker:

marker
^^^^^^

* **marker** *<marker_word>*

The marker command adds a 32-bit marker word into the data stream. This can be used to separate data
from different modules.

.. _vme-command-setbase:
.. _vme-command-resetbase:

setbase/resetbase
^^^^^^^^^^^^^^^^^

* **setbase** *<address>*
* **resetbase**

These commands can be used to temporarily replace the current base address with a different value.
**setbase** sets a new base address, which will be effective for all following commands. Use
**resetbase** to restore the original base address.

.. _vme-command-write-float-word:

write_float_word
^^^^^^^^^^^^^^^^

* **write_float_word** *<address_mode>* *<address>* *<part>* *<value>*

The write_float_word command is a helper function for dealing with VME modules
using IEEE-754 floating point numbers internally (e.g. the ISEG VHS4030). The
command writes a 16-bit part of a 32-bit float into the given register without
performing any integer conversions.

Arguments:

* *address_mode*

  The VME address mode: a16, a24 or a32

* *address*

  Address of the register to write to.

* *part*

  One of **upper** / **1** and  **lower** / **0**. The upper part contains the
  16 most significant bits of the float, the lower part the 16 least
  significant bits.

* *value*

  The floating point value using a *.* as the decimal separator.

Example
::

  write_float_word a16 0x0014 upper 3.14
  write_float_word a16 0x0016 lower 3.14

Writes the 32-bit float value *3.14* to the two 16-bit registers 0x14 and 0x16.

print
^^^^^

Prints its arguments to the log output on a separate line. Arguments are
separated by a space by default which means string quoting is not
strictly required.

Example
::

  print "Hello World!"
  print Hello World!  # Quoting the string is optional

.. index:: VME Script Accumulator Commands
.. _vme_script_accu_commands:

.. _vme-command-accu-set:

accu_set
^^^^^^^^

* **accu_set** *<value>* Set the script local accumulator to a constant value.

.. _vme-command-accu-mask-rotate:

accu_mask_rotate
^^^^^^^^^^^^^^^^

* **accu_mask_rotate** *<mask>* *<rotate_amount>*

First applies the 32 bit *<mask>* value to the accu, then left rotates the accu
by *<rotate_amount>* bits.

.. _vme-command-accu-test:

accu_test
^^^^^^^^^

* **accu_test** *<compare_op>* *<compare_value>* *<message>*

Compares the current accumulator value against the constant *<compare_value>*
and prints a message containing the result.

.. table:: accu_test comparison operators
  :name: accu_test_comparison_operators

  +-----+----+
  | eq  | == |
  +-----+----+
  | neq | != |
  +-----+----+
  | lt  | <  |
  +-----+----+
  | lte | <= |
  +-----+----+
  | gt  | >  |
  +-----+----+
  | gte | >= |
  +-----+----+

.. index:: MVLC VME Script Commands
.. _vme_script_mvlc_commands:


MVLC special commands
~~~~~~~~~~~~~~~~~~~~~

.. _vme_command-mvlc_set_address_inc_mode:

mvlc_set_address_inc_mode
^^^^^^^^^^^^^^^^^^^^^^^^^

* **mvlc_set_address_inc_mode** *('fifo'|'mem')*

Sets the address increment mode for subsequent VME block reads. If *fifo* is
selected the read address will not be incremented.

.. _vme_command-mvlc_wait:

mvlc_wait
^^^^^^^^^

* **mvlc_wait** *<clocks>*

Delay execution of the following stack commands by the given number of clock
cycles. The maximum value of the *clocks* argument is ``(2**24) - 1``.

.. _vme_command-mvlc_signal_accu:

mvlc_signal_accu
^^^^^^^^^^^^^^^^

* **mvlc_signal_accu**

Signal the internal accumulator of the current command stack.

.. _vme_command-mvlc_mask_shift_accu:

mvlc_mask_shift_accu
^^^^^^^^^^^^^^^^^^^^

* **mvlc_mask_shift_accu** *<mask>* *<shift>*

Set the stack accumulator *mask* and *shift* values. When the accumulator value
is used for a VME block read first the mask is applied, then the result is
rotated left by the *shift* argument. The resulting value is the number of
block read cycles to perform.


.. _vme_command-mvlc_set_accu:

mvlc_set_accu
^^^^^^^^^^^^^

* **mvlc_set_accu** *<value>*

Set the stack accumulator to the given 32-bit value.


.. _vme_command-mvlc_read_to_accu:

mvlc_read_to_accu
^^^^^^^^^^^^^^^^^
* **mvlc_read_to_accu** *<amode> <dwidth> <address> ['slow']*

Same as :ref:`read <vme-command-read>` but reads into the stack accumulator instead of
outputting to the data stream.

.. _vme_command-mvlc_compare_loop_accu:

mvlc_compare_loop_accu
^^^^^^^^^^^^^^^^^^^^^^

* **mvlc_compare_loop_accu** *('eq'|'lt'|'gt')* *<value>*

Repeatedly compares the value stored in the accumulator to the given *value*
using the specified comparison operator. If the comparison fails jumps to the
previous stack command, otherwise proceeds to the next command.

.. _vme_command-mvlc_writespecial:

mvlc_writespecial
^^^^^^^^^^^^^^^^^

* **mvlc_writespecial** *('timestamp'|'accu'|<numeric_value>)*

Writes the value of the specified internal MVLC variable to the current output.

.. _vme_command-mvlc_stack_begin:

mvlc_stack_begin/end
^^^^^^^^^^^^^^^^^^^^

Allows to specify that a group of VME Script commands should be executed as a
single MVLC command stack instead of running each command individually. This
allows making use of features that require a stack context, e.g. the MVLC stack
accumulator logic.

mvlc_stack_begin/end only has an effect when manually executing the VME Script,
e.g. via *Run* in the VME Script Editor. During the DAQ initialization phase,
if a script that is part of a MVCL readout stack (e.g. module readout) is
processed, the mvlc_stack_begin/end lines do not have any effect (the commands
inside the block still do!).

.. _vme_command-mvlc_custom_begin:

mvlc_custom_begin/end
^^^^^^^^^^^^^^^^^^^^^

This is an advanced block command allowing to specify arbitrary stack data to
upload and run on the MVLC. Example:

::

   mvlc_custom_begin output_words=2
     0x140D0001	  # read 16 bit word to accu
     0x00006030	  #  from address 6030
     0xC5000000	  # shift mask: no rotation
     0x0000FFFF	  # mask lower 16 bits
     0xC1000001	  # write_special_word = 1 (Accu)
   mvlc_custom_end

The *output_words* argument specifies the expected, fixed number of output
words the execution of the custom stack will produce.


VMUSB specific
~~~~~~~~~~~~~~
.. _vme_command-vmusb-write-reg:

* **vmusb_write_reg** *(<register_address>|<register_name>) <value>*
* **vmusb_read_reg** *(<register_address>|<register_name>)*

These commands only work when using the WIENER VM-USB controller and allow
read/write access to its internal registers. For details on the registers see
the VM-USB manual section *3.4 - Internal Register File*.

Instead of using register addresses some registers are also accessible via
name. The following name mappings are defined:

.. table:: VMUSB Register Names
  :name: vmusb-register-names

  +-------------------+-------------+
  | **Register Name** | **address** |
  +===================+=============+
  | dev_src           | 0x10        |
  +-------------------+-------------+
  | dgg_a             | 0x14        |
  +-------------------+-------------+
  | dgg_b             | 0x18        |
  +-------------------+-------------+
  | dgg_ext           | 0x38        |
  +-------------------+-------------+
  | sclr_a            | 0x1c        |
  +-------------------+-------------+
  | sclr_b            | 0x20        |
  +-------------------+-------------+
  | daq_settings      | 0x08        |
  +-------------------+-------------+

Floating Point Values, Variables and Mathematical Expressions
-------------------------------------------------------------
Since mvme-0.9.7 VME scripts support evaluation of numerical expressions and
can contain references to variables. Additionally floating point values can be
used where previously only unsigned integers where allowed.

It is up to each specific command how floating point values are interpreted and
what limits are imposed. The VME read and write commands use mathematical
rounding and test that the resulting value fits in an unsigned 16 or 32 bit
integer (depending on the commands data width argument). On the other hand the
:ref:`vme-command-write-float-word` command uses the floating point value
directly without performing an integer conversion.

Variables
~~~~~~~~~
The variable system in VME Scripts is based on simple string replacement.
Whenever a variable reference of the form ``${varname}`` is encountered the
value stored under the name ``varname`` is looked up and is used to replace the
variable reference. Variable expansion is currently not recursive so
``${${foo}}`` will try to look up the value of a variable named ``${foo}``.

Variables are stored in lists of symbol tables with the variables from the
first (innermost) table overriding those defined in the outer scopes.

Each object in the VME Config tree carries a symbol table: VME Events, VME
Modules and VME Script objects each have a set of variables attached to them.
When parsing a VME script the list symbol tables is assembled by traversing the
VME Config tree upwards towards the root node. Each objects symbol table is
appened to the list of tables. This way variables defined at script scope take
precedence over those defined at module scope. The same is true for module and
event scopes.

In addition to variables defined by VME Config objects variables can also be
locally defined inside a VME Script using the ``set`` command. The variable
will be entered into the most local symbol table and will override any other
definition of a variable with the same name.

The mvme GUI currently contains a dedicated editor for variables defined at VME
Event scope. Select an event in the VME Config tree and click the **Edit
Variables** button above the tree. Module level variables can be accessed via
**Edit Module Settings** from the context menu. A dedicated editor for Module
and Script objects is going to be added in the future.

Example
^^^^^^^
::

   set threshold 500
   write a32 d16 0x1234 ${threshold}   # -> write a32 d16 0x1234 500

   set addr 0x6789
   set value 0b1010

   write a32 d16 ${addr} ${value}      # -> write a32 d16 0x6789 0b1010
   ${addr} ${value}                    # same as above using the short form of the write command


Expressions
~~~~~~~~~~~

.. _exprtk: http://www.partow.net/programming/exprtk/index.html

Mathematical expressions in VME scripts are enclosed between ``$(`` and ``)``.
The enclosed string (including the outermost parentheses) is passed to the
`exprtk`_ library for evaluation and the resulting value replaces the
expression string before further parsing is done.

exprtk internally uses floating point arithmetic and the result of evaluating
an expression is always a floating point value. It is up to the specific
command of how the value is treated.

Variable references inside expressions are expanded before the expression is
given to the `exprtk`_ library for evaluation.

Example
^^^^^^^
::

   # From the MDPP-32-QDC init script: Window start = 16384  + delay[ns] / 1.56;
   0x6050  $(16384 - 100 / 1.56)

   # or using a local variable to hold the delay:
   set my_delay -100
   0x6050  $(16384 + ${my_delay} / 1.56)



Example Script
--------------
::

    # BLT readout until BERR or number of transfers reached
    bltfifo a32 0x0000 10000

    # Write the value 3 to address 0x6070. If this appears in a module specific
    # script (init, readout, reset) the module base address is added to the
    # given address.
    0x6070 3

    # Same as above but explicitly using the write command.
    write a32 d16 0x6070 3

    # Set a different base address. This will replace the current base address
    # until resetbase is used.
    setbase 0xbb000000

    # Results in an a32/d16 write to 0xbb006070.
    0x6070 5

    # Restore the original base address.
    resetbase

    # Binary notation for the register value.
    0x6070 0b0000'0101
