#include <QRegularExpression>
#include <QDebug>
#include "typedefs.h"

const QString eventSample = R"-(
# Start acquisition sequence using the default multicast address 0xbb
writeabs a32 d16 0xbc00603a      0   # stop acq
writeabs a32 d16 0xbc006090      3   # reset CTRA and CTRB
writeabs a32 d16 0xbc00603c      1   # FIFO reset
writeabs a32 d16 0xbc00603a      1   # start acq
writeabs a32 d16 0xbc006034      1   # readout reset
)-";

const QString moduleSample = R"-(
# Settings related to the readout loop
# #####################################
0x6010  2           # irq level
0x6012  0           # irq vector

# IRQ_source and thresholds
#0x601C 1           # 1 -> specifies number of words
#0x6018  100        # IRQ-FIFO threshold, words
0x601C 0            # 0 -> the following register specifies the number of events
0x601E 100            # IRQ-FIFO threshold, events

# marking_type
0x6038 0x1          # End Of Event marking
                    # 0 -> event counter
                    # 1 -> time stamp
                    # 3 -> extended time stamp

# multi event mode:
0x6036  0xb         # 0x0 -> single event
                    # 0x3 -> multi event, number of words
                    # 0xb -> multievent, transmits number of events specified

# max_transfer_data
0x601A  1           # multi event mode == 0x3 -> Berr is emitted when more or equal the
                    #   specified number of words have been sent and "End Of Event" is detected.
                    # multi event mode == 0xb -> Berr is emitted when the specified number
                    #   of events has been transmitted.

# MCST - Multicast Setup
# #####################################
0x6020 0x80         # Enable multicast
0x6024 0xBC         # Set 8 high bits of MCST address
)-";

struct ReplacementRule
{
    struct Options
    {
        static const uint16_t KeepOriginalAsComment = 0;
        static const uint16_t ReplaceOnly = 1;
    };

    QString pattern;
    QString replacement;
    uint16_t options = Options::KeepOriginalAsComment;
};

QString apply_replacement_rules(
    const QVector<ReplacementRule> &rules,
    const QString &input,
    const QString &commentPrefix = {})
{
    using RO = ReplacementRule::Options;

    QString result = input;

    for (const auto &rule: rules)
    {
        QRegularExpression re(rule.pattern, QRegularExpression::MultilineOption);

        QString replacement;

        if (rule.options & RO::ReplaceOnly)
        {
            replacement = rule.replacement;
        }
        else if (rule.options == RO::KeepOriginalAsComment)
        {
            replacement = "# " + commentPrefix + " \\1\n" + rule.replacement;
        }

        result.replace(re, replacement);
    }

    return result;
}

// Guess the high-byte of the event mcst.
// The input script must be one of the event daq start/stop scripts containing
// a write to the 'start/stop acq register' (0x603a).
u8 guess_event_mcst(const QString &eventScript)
{
    static const QRegularExpression re(
        R"-(^\s*writeabs\s+a32\s+d16\s+(0x[0-9a-fA-F]{2})00603a\s+.*$)-",
        QRegularExpression::MultilineOption);

    auto match = re.match(eventScript);

    if (match.hasMatch())
    {
        //qDebug() << "re has match" << match.captured(0) << match.captured(1);
        u8 mcst = static_cast<u8>(match.captured(1).toUInt(nullptr, 0));
        return mcst;
    }

    return 0u;
}


int main(int argc, char *argv[])
{
    {
        u8 guessedMCST = guess_event_mcst(eventSample);
        qDebug() << "guessed event mcst =" <<  static_cast<unsigned>(guessedMCST);
    }

    {
        // For event level scripts event_daq_start, event_daq_stop,
        // readout_cylce_start, readout_cycle_end.
        QVector<ReplacementRule> eventRules =
        {
            {
                R"-(^# Start acquisition sequence using the default multicast address 0xbb\s*$)-",
                "# Run the start-acquisition-sequence for all modules via the events multicast address.",
                ReplacementRule::Options::ReplaceOnly,
            },
            {
                R"-(^(\s*writeabs\s+a32\s+d16\s+0x[0-9a-fA-F]{2}00603a\s+0.*)$)-",
                "writeabs a32 d16 0x${mesy_mcst}00603a      0   # stop acq",
            },
            {
                R"-(^(\s*writeabs\s+a32\s+d16\s+0x[0-9a-fA-F]{2}006090\s+3.*)$)-",
                "writeabs a32 d16 0x${mesy_mcst}006090      3   # reset CTRA and CTRB",
            },
            {
                R"-(^(\s*writeabs\s+a32\s+d16\s+0x[0-9a-fA-F]{2}00603c\s+1.*)$)-",
                "writeabs a32 d16 0x${mesy_mcst}00603c      1   # FIFO reset",
            },
            {
                R"-(^(\s*writeabs\s+a32\s+d16\s+0x[0-9a-fA-F]{2}00603a\s+1.*)$)-",
                "writeabs a32 d16 0x${mesy_mcst}00603a      1   # start acq",
            },
            {
                R"-(^(\s*writeabs\s+a32\s+d16\s+0x[0-9a-fA-F]{2}006034\s+1.*)$)-",
                "writeabs a32 d16 0x${mesy_mcst}006034      1   # readout reset",
            },
        };

        QString updated = apply_replacement_rules(eventRules, eventSample, "next line auto updated by mvme -");

        qDebug().noquote() << "\neventSample after updating with eventRules:\n" << updated;
    }

    {
        QVector<ReplacementRule> moduleRules =
        {
            // irq level
            {
                R"-(^(\s*0x6010\s+[1-7]{1}.*)$)-",
                "0x6010  ${sys_irq}                                 # irq level",
            },

            // remove the irq vector line
            {
                R"-(^(\s*0x6012\s+0.*)$)-",
                "",
                ReplacementRule::Options::ReplaceOnly,
            },

            // fifo irq threshold
            {
                R"-(^(\s*0x601E\s+[0-9]+.*)$)-",
                "0x601E $(${mesy_readout_num_events} + 1)           # IRQ-FIFO threshold, events",
            },

            {
                R"-(^(\s*0x601A\s+[0-9]+.*)$)-",
                "0x601A ${mesy_readout_num_events}                  # multi event mode == 0x3 -> "
                "Berr is emitted when more or equal the",
            },

            // end of event marker
            {
                R"-(^(\s*0x6038\s+.*)$)-",
                "0x6038 ${mesy_eoe_marker}                          # End Of Event marking",
            },

            // set mcst
            {
                R"-(^(\s*0x6024\s+0x[0-9a-fA-F]{2})$)-",
                "0x6024 0x${mesy_mcst}                              # Set 8 high bits of MCST address",
            },
        };

        QString updated = apply_replacement_rules(moduleRules, moduleSample, "next line auto updated by mvme -");

        qDebug().noquote() << "\nmoduleSample after updating with eventRules:\n" << updated;
    }

    return 0;
}

#if 0
    QRegularExpression reRule(
        R"-(^(\s*writeabs\s+a32\s+d16\s+0x[0-9a-fA-F]{2}00603a\s+0.*)$)-",
        QRegularExpression::MultilineOption);

    auto matchIter = reRule.globalMatch(eventSample);

    while (matchIter.hasNext())
    {
        auto match = matchIter.next();
        qDebug() << "match:" << match.captured();
    }

    const QString replacement = "# updated by mvme - \\1\nwriteabs a32 d16 0x${mesy_mcst}00603a    0    # stop acq";
    QString updated = eventSample;

    updated.replace(reRule, replacement);

    qDebug().noquote() << "\nsample after updating:\n" << updated;
#endif


