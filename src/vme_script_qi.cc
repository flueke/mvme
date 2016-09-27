#include "vme_script_qi.h"

#define BOOST_SPIRIT_DEBUG

#include <boost/config/warning_disable.hpp>

#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/phoenix.hpp>

#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/fusion/include/io.hpp>

#if 1
BOOST_FUSION_ADAPT_STRUCT(
    vme_script::VMEScript,
    (std::vector<vme_script::Command>, commands)
)

BOOST_FUSION_ADAPT_STRUCT(
    vme_script::Command,
    (vme_script::Command::Type, type)
    (vme_script::Command::AddressMode, addressMode)
    (vme_script::Command::DataWidth, dataWidth)
)
#endif

namespace vme_script
{
    namespace qi = boost::spirit::qi;
    namespace ascii = boost::spirit::ascii;
    namespace phoenix = boost::phoenix;

    using namespace qi::labels;
    using phoenix::construct;
    using phoenix::val;

    template<typename Iterator, typename Skipper>
    struct VMEScriptGrammar
        //: qi::grammar<Iterator, VMEScript(), qi::locals<std::string>, ascii::space_type>
        : qi::grammar<Iterator, Skipper, VMEScript()>
    {
        VMEScriptGrammar()
            : VMEScriptGrammar::base_type(start, "VMEScript")
        {
            start = *(command | qi::omit[comment]);
            comment = qi::char_('#') > *(qi::char_ - qi::eol) > *qi::eol;
            command = write_command | read_command;

            write_command
                = ascii::string("write")[_val = Command::Write]
                > (-address_mode | qi::attr(Command::A32))
                > (-data_width | qi::attr(Command::D16))
                > number
                > number
                ;

            read_command
                = ascii::string("read")[_val = Command::Read]
                > (-address_mode | qi::attr(Command::A32))
                > (-data_width | qi::attr(Command::D16))
                > number
                ;

            address_mode
                = ascii::string("a16")[_val = Command::A16]
                | ascii::string("a24")[_val = Command::A24]
                | ascii::string("a32")[_val = Command::A32]
                ;

            data_width
                = ascii::string("d16")[_val = Command::D16]
                | ascii::string("d32")[_val = Command::D32]
                ;

            number
                = qi::lit("0x") > qi::hex
                | qi::uint_;


            start.name("start");
            comment.name("comment");
            command.name("command");
            write_command.name("write_command");
            read_command.name("read_command");
            address_mode.name("address_mode");
            data_width.name("data_width");
            number.name("number");

            qi::on_error<qi::fail> (
                start,
                std::cout
                << val("Error! Expecting ")
                << _4
                << val(" here: \"")
                << construct<std::string>(_3, _2)
                << val("\"")
                << ", \"" << construct<std::string>(_1, _2) << "\""
                << std::endl
            );

#if 1
            BOOST_SPIRIT_DEBUG_NODES(
                (start)
                (comment)
                (command)
                (write_command)
                (read_command)
                (address_mode)
                (data_width)
                (number)
                );
#endif
        }

        qi::rule<Iterator, Skipper, VMEScript()> start;
        qi::rule<Iterator, Skipper> command;
        qi::rule<Iterator, Skipper, Command()> write_command;
        qi::rule<Iterator, Skipper, Command()> read_command;
        qi::rule<Iterator, Skipper, Command::AddressMode()> address_mode;
        qi::rule<Iterator, Skipper, Command::DataWidth()> data_width;
        qi::rule<Iterator, Skipper, uint32_t()> number;
        qi::rule<Iterator> comment;
    };
}

int main(int argc, char *argv[])
{
    std::cout << "/////////////////////////////////////////////////////////\n\n";
    std::cout << "\t\tVME-Script Parser...\n\n";
    std::cout << "/////////////////////////////////////////////////////////\n\n";

    using boost::spirit::ascii::space;
    typedef std::string::const_iterator IteratorType;
    typedef boost::spirit::ascii::space_type SkipperType;
    typedef vme_script::VMEScriptGrammar<IteratorType, SkipperType> VMEScriptParser;

    VMEScriptParser g; // Our grammar
    std::string str;
    while (getline(std::cin, str))
    {
        if (str.empty())
            break;

        vme_script::VMEScript vmeScript;

        std::string::const_iterator iter = str.begin();
        std::string::const_iterator end = str.end();

        bool r = phrase_parse(iter, end, g, space, vmeScript);

        if (r && iter == end)
        {
            std::cout << boost::fusion::tuple_open('[');
            std::cout << boost::fusion::tuple_close(']');
            std::cout << boost::fusion::tuple_delimiter(", ");

            std::cout << "-------------------------\n";
            std::cout << str << std::endl;
            std::cout << "Parsing succeeded\n";
            //std::cout << "got: " << boost::fusion::as_vector(vmeScript) << std::endl;
            std::cout << "\n-------------------------\n";
        }
        else
        {
            std::cout << "-------------------------\n";
            std::cout << str << std::endl;
            std::cout << "Parsing failed\n";
            std::cout << "-------------------------\n";
        }
    }

    std::cout << "Bye... :-) \n\n";
    return 0;
}
