#include "vme_script.h"

#define BOOST_SPIRIT_DEBUG

#include <boost/config/warning_disable.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/include/phoenix_object.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/fusion/include/io.hpp>

BOOST_FUSION_ADAPT_STRUCT(
    vme_script::VMEScript,
    (std::vector<vme_script::VMEScriptCommand>, commands)
)

BOOST_FUSION_ADAPT_STRUCT(
    vme_script::VMEScriptCommand,
    (vme_script::VMEScriptCommand::Type, type)
)

namespace vme_script
{
    namespace qi = boost::spirit::qi;
    namespace ascii = boost::spirit::ascii;
    namespace phoenix = boost::phoenix;

    using namespace qi::labels;
    using phoenix::construct;
    using phoenix::val;

    template<typename Iterator>
    struct VMEScriptGrammar
        //: qi::grammar<Iterator, VMEScript(), qi::locals<std::string>, ascii::space_type>
        : qi::grammar<Iterator, VMEScript(), ascii::space_type>
    {
        VMEScriptGrammar()
            : VMEScriptGrammar::base_type(start, "VMEScript")
        {
            //start = *(comment | command);
            start = *comment;
            comment = qi::lit('#') > qi::lexeme[*(qi::char_ - qi::eol)] > qi::eol;

            start.name("start");
            comment.name("comment");
            command.name("command");

            qi::on_error<qi::fail> (
                start,
                std::cout
                << val("Error! Expecting ")
                << _4
                << val(" here: \"")
                << construct<std::string>(_3, _2)
                << val("\"")
                << std::endl
            );

            BOOST_SPIRIT_DEBUG_NODE(start);
        }

        qi::rule<Iterator, VMEScript(), ascii::space_type> start;
        qi::rule<Iterator, void()> comment;
        qi::rule<Iterator, VMEScriptCommand(), ascii::space_type> command;
    };
}

int main(int argc, char *argv[])
{
    std::cout << "/////////////////////////////////////////////////////////\n\n";
    std::cout << "\t\tVME-Script Parser...\n\n";
    std::cout << "/////////////////////////////////////////////////////////\n\n";

    using boost::spirit::ascii::space;
    typedef std::string::const_iterator iterator_type;
    typedef vme_script::VMEScriptGrammar<iterator_type> vme_script_parser;

    vme_script_parser g; // Our grammar
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
            std::cout << "Parsing succeeded\n";
            //std::cout << "got: " << boost::fusion::as_vector(vmeScript) << std::endl;
            std::cout << "\n-------------------------\n";
        }
        else
        {
            std::cout << "-------------------------\n";
            std::cout << "Parsing failed\n";
            std::cout << "-------------------------\n";
        }
    }

    std::cout << "Bye... :-) \n\n";
    return 0;
}
