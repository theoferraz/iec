/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2021, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include "program_options_lite.h"

using namespace std;

//! \ingroup TAppCommon
//! \{

namespace df
{
  namespace program_options_lite
  {
    ErrorReporter default_error_reporter;

    ostream& ErrorReporter::error(const string& where)
    {
      is_errored = 1;
      cerr << where << " error: ";
      return cerr;
    }

    ostream& ErrorReporter::warn(const string& where)
    {
      cerr << where << " warning: ";
      return cerr;
    }

    Options::~Options()
    {
      for(Options::NamesPtrList::iterator it = opt_list.begin(); it != opt_list.end(); it++)
      {
        delete *it;
      }
    }

    void Options::addOption(OptionBase *opt)
    {
      Names* names = new Names();
      names->opt = opt;
      string& opt_string = opt->opt_string;

      size_t opt_start = 0;
      for (size_t opt_end = 0; opt_end != string::npos;)
      {
        opt_end = opt_string.find_first_of(',', opt_start);
        bool force_short = 0;
        if (opt_string[opt_start] == '-')
        {
          opt_start++;
          force_short = 1;
        }
        string opt_name = opt_string.substr(opt_start, opt_end - opt_start);
        if (force_short || opt_name.size() == 1)
        {
          names->opt_short.push_back(opt_name);
          opt_short_map[opt_name].push_back(names);
        }
        else
        {
#if JVET_O0549_ENCODER_ONLY_FILTER_POL
          if (opt_name.size() > 0 && opt_name.back() == '*')
          {
            string prefix_name = opt_name.substr(0, opt_name.size() - 1);
            names->opt_prefix.push_back(prefix_name);
            opt_prefix_map[prefix_name].push_back(names);
          }
          else
          {
            names->opt_long.push_back(opt_name);
            opt_long_map[opt_name].push_back(names);
          }
#else
          names->opt_long.push_back(opt_name);
          opt_long_map[opt_name].push_back(names);
#endif
        }
        opt_start += opt_end + 1;
      }
      opt_list.push_back(names);
    }

    /* Helper method to initiate adding options to Options */
    OptionSpecific Options::addOptions()
    {
      return OptionSpecific(*this);
    }

    static void setOptions(Options::NamesPtrList& opt_list, const string& value, ErrorReporter& error_reporter)
    {
      /* multiple options may be registered for the same name:
       *   allow each to parse value */
      for (Options::NamesPtrList::iterator it = opt_list.begin(); it != opt_list.end(); ++it)
      {
        (*it)->opt->parse(value, error_reporter);
      }
    }

    static const char spaces[41] = "                                        ";

    /* format help text for a single option:
     * using the formatting: "-x, --long",
     * if a short/long option isn't specified, it is not printed
     */
    static void doHelpOpt(ostream& out, const Options::Names& entry, unsigned pad_short = 0)
    {
      pad_short = min(pad_short, 8u);

      if (!entry.opt_short.empty())
      {
        unsigned pad = max((int)pad_short - (int)entry.opt_short.front().size(), 0);
        out << "-" << entry.opt_short.front();
        if (!entry.opt_long.empty())
        {
          out << ", ";
        }
        out << &(spaces[40 - pad]);
      }
      else
      {
        out << "   ";
        out << &(spaces[40 - pad_short]);
      }

      if (!entry.opt_long.empty())
      {
        out << "--" << entry.opt_long.front();
      }
#if JVET_O0549_ENCODER_ONLY_FILTER_POL
      else if (!entry.opt_prefix.empty())
      {
      out << "--" << entry.opt_prefix.front() << "*";
      }
#endif
    }

    /* format the help text */
    void doHelp(ostream& out, Options& opts, unsigned columns)
    {
      const unsigned pad_short = 3;
      /* first pass: work out the longest option name */
      unsigned max_width = 0;
      for(Options::NamesPtrList::iterator it = opts.opt_list.begin(); it != opts.opt_list.end(); it++)
      {
        ostringstream line(ios_base::out);
        doHelpOpt(line, **it, pad_short);
        max_width = max(max_width, (unsigned) line.tellp());
      }

      unsigned opt_width = min(max_width+2, 28u + pad_short) + 2;
      unsigned desc_width = columns - opt_width;

      /* second pass: write out formatted option and help text.
       *  - align start of help text to start at opt_width
       *  - if the option text is longer than opt_width, place the help
       *    text at opt_width on the next line.
       */
      for(Options::NamesPtrList::iterator it = opts.opt_list.begin(); it != opts.opt_list.end(); it++)
      {
        ostringstream line(ios_base::out);
        line << "  ";
        doHelpOpt(line, **it, pad_short);

        const string& opt_desc = (*it)->opt->opt_desc;
        if (opt_desc.empty())
        {
          /* no help text: output option, skip further processing */
          cout << line.str() << endl;
          continue;
        }
        size_t currlength = size_t(line.tellp());
        if (currlength > opt_width)
        {
          /* if option text is too long (and would collide with the
           * help text, split onto next line */
          line << endl;
          currlength = 0;
        }
        /* split up the help text, taking into account new lines,
         *   (add opt_width of padding to each new line) */
        for (size_t newline_pos = 0, cur_pos = 0; cur_pos != string::npos; currlength = 0)
        {
          /* print any required padding space for vertical alignment */
          line << &(spaces[40 - opt_width + currlength]);
          newline_pos = opt_desc.find_first_of('\n', newline_pos);
          if (newline_pos != string::npos)
          {
            /* newline found, print substring (newline needn't be stripped) */
            newline_pos++;
            line << opt_desc.substr(cur_pos, newline_pos - cur_pos);
            cur_pos = newline_pos;
            continue;
          }
          if (cur_pos + desc_width > opt_desc.size())
          {
            /* no need to wrap text, remainder is less than avaliable width */
            line << opt_desc.substr(cur_pos);
            break;
          }
          /* find a suitable point to split text (avoid spliting in middle of word) */
          size_t split_pos = opt_desc.find_last_of(' ', cur_pos + desc_width);
          if (split_pos != string::npos)
          {
            /* eat up multiple space characters */
            split_pos = opt_desc.find_last_not_of(' ', split_pos) + 1;
          }

          /* bad split if no suitable space to split at.  fall back to width */
          bool bad_split = split_pos == string::npos || split_pos <= cur_pos;
          if (bad_split)
          {
            split_pos = cur_pos + desc_width;
          }
          line << opt_desc.substr(cur_pos, split_pos - cur_pos);

          /* eat up any space for the start of the next line */
          if (!bad_split)
          {
            split_pos = opt_desc.find_first_not_of(' ', split_pos);
          }
          cur_pos = newline_pos = split_pos;

          if (cur_pos >= opt_desc.size())
          {
            break;
          }
          line << endl;
        }

        cout << line.str() << endl;
      }
    }

    struct OptionWriter
    {
      OptionWriter(Options& rOpts, ErrorReporter& err)
      : opts(rOpts), error_reporter(err)
      {}
      virtual ~OptionWriter() {}

      virtual const string where() = 0;

      bool storePair(bool allow_long, bool allow_short, const string& name, const string& value);
      bool storePair(const string& name, const string& value)
      {
        return storePair(true, true, name, value);
      }

      Options& opts;
      ErrorReporter& error_reporter;
    };

    bool OptionWriter::storePair(bool allow_long, bool allow_short, const string& name, const string& value)
    {
      bool found = false;
#if JVET_O0549_ENCODER_ONLY_FILTER_POL
      std::string val = value;
#endif
      Options::NamesMap::iterator opt_it;
      if (allow_long)
      {
        opt_it = opts.opt_long_map.find(name);
        if (opt_it != opts.opt_long_map.end())
        {
          found = true;
        }
      }

      /* check for the short list */
      if (allow_short && !(found && allow_long))
      {
        opt_it = opts.opt_short_map.find(name);
        if (opt_it != opts.opt_short_map.end())
        {
          found = true;
        }
      }
#if JVET_O0549_ENCODER_ONLY_FILTER_POL
      bool allow_prefix = allow_long;
      if (allow_prefix && !found)
      {
        for (opt_it = opts.opt_prefix_map.begin(); opt_it != opts.opt_prefix_map.end(); opt_it++)
        {
          std::string name_prefix = name.substr(0, opt_it->first.size());
          if (name_prefix == opt_it->first)
          {
            // prepend value matching *
            val = name.substr(name_prefix.size()) + std::string(" ") + val;
            found = true;
            break;
          }
        }
      }
#endif
      if (!found)
      {
        error_reporter.error(where())
          << "Unknown option `" << name << "' (value:`" << value << "')\n";
        return false;
      }
#if JVET_O0549_ENCODER_ONLY_FILTER_POL
      setOptions((*opt_it).second, val, error_reporter);
#else
      setOptions((*opt_it).second, value, error_reporter);
#endif
      return true;
    }

    struct ArgvParser : public OptionWriter
    {
      ArgvParser(Options& rOpts, ErrorReporter& rError_reporter)
      : OptionWriter(rOpts, rError_reporter)
      {}

      const string where() { return "command line"; }

      unsigned parseGNU(unsigned argc, const char* argv[]);
      unsigned parseSHORT(unsigned argc, const char* argv[]);
    };

    /**
     * returns number of extra arguments consumed
     */
    unsigned ArgvParser::parseGNU(unsigned argc, const char* argv[])
    {
      /* gnu style long options can take the forms:
       *  --option=arg
       *  --option arg
       */
      string arg(argv[0]);
      size_t arg_opt_start = arg.find_first_not_of('-');
      size_t arg_opt_sep = arg.find_first_of('=');
      string option = arg.substr(arg_opt_start, arg_opt_sep - arg_opt_start);

      unsigned extra_argc_consumed = 0;
      if (arg_opt_sep == string::npos)
      {
        /* no argument found => argument in argv[1] (maybe) */
        /* xxx, need to handle case where option isn't required */
        if(!storePair(true, false, option, "1"))
        {
          return 0;
        }
      }
      else
      {
        /* argument occurs after option_sep */
        string val = arg.substr(arg_opt_sep + 1);
        storePair(true, false, option, val);
      }

      return extra_argc_consumed;
    }

    unsigned ArgvParser::parseSHORT(unsigned argc, const char* argv[])
    {
      /* short options can take the forms:
       *  --option arg
       *  -option arg
       */
      string arg(argv[0]);
      size_t arg_opt_start = arg.find_first_not_of('-');
      string option = arg.substr(arg_opt_start);
      /* lookup option */

      /* argument in argv[1] */
      /* xxx, need to handle case where option isn't required */
      if (argc == 1)
      {
        error_reporter.error(where())
          << "Not processing option `" << option << "' without argument\n";
        return 0; /* run out of argv for argument */
      }
      storePair(false, true, option, string(argv[1]));

      return 1;
    }

    list<const char*>
    scanArgv(Options& opts, unsigned argc, const char* argv[], ErrorReporter& error_reporter)
    {
      ArgvParser avp(opts, error_reporter);

      /* a list for anything that didn't get handled as an option */
      list<const char*> non_option_arguments;

      for(unsigned i = 1; i < argc; i++)
      {
        if (argv[i][0] != '-')
        {
          non_option_arguments.push_back(argv[i]);
          continue;
        }

        if (argv[i][1] == 0)
        {
          /* a lone single dash is an argument (usually signifying stdin) */
          non_option_arguments.push_back(argv[i]);
          continue;
        }

        if (argv[i][1] != '-')
        {
          /* handle short (single dash) options */
          i += avp.parseSHORT(argc - i, &argv[i]);
          continue;
        }

        if (argv[i][2] == 0)
        {
          /* a lone double dash ends option processing */
          while (++i < argc)
          {
            non_option_arguments.push_back(argv[i]);
          }
          break;
        }

        /* handle long (double dash) options */
        i += avp.parseGNU(argc - i, &argv[i]);
      }

      return non_option_arguments;
    }

    struct CfgStreamParser : public OptionWriter
    {
      CfgStreamParser(const string& rName, Options& rOpts, ErrorReporter& rError_reporter)
      : OptionWriter(rOpts, rError_reporter)
      , name(rName)
      , linenum(0)
      {}

      const string name;
      int linenum;
      const string where()
      {
        ostringstream os;
        os << name << ":" << linenum;
        return os.str();
      }

      bool scanLine(string& line, string& option, string& value);
      void scanStream(istream& in);
    };

    bool CfgStreamParser::scanLine(string& line, string& option, string& value)
    {
      /* strip any leading whitespace */
      size_t start = line.find_first_not_of(" \t\n\r");
      if (start == string::npos)
      {
        /* blank line */
        return false;
      }
      if (line[start] == '#')
      {
        /* comment line */
        return false;
      }
      /* look for first whitespace or ':' after the option end */
      size_t option_end = line.find_first_of(": \t\n\r",start);
      option            = line.substr(start, option_end - start);

      /* look for ':', eat up any whitespace first */
      start = line.find_first_not_of(" \t\n\r", option_end);
      if (start == string::npos)
      {
        /* error: badly formatted line */
        error_reporter.warn(where()) << "line formatting error\n";
        return false;
      }
      if (line[start] != ':')
      {
        /* error: badly formatted line */
        error_reporter.warn(where()) << "line formatting error\n";
        return false;
      }

      /* look for start of value string -- eat up any leading whitespace */
      start = line.find_first_not_of(" \t\n\r", ++start);
      if (start == string::npos)
      {
        /* error: badly formatted line */
        error_reporter.warn(where()) << "line formatting error\n";
        return false;
      }

      /* extract the value part, which may contain embedded spaces
       * by searching for a word at a time, until we hit a comment or end of line */
      size_t value_end = start;
      do
      {
        if (line[value_end] == '#')
        {
          /* rest of line is a comment */
          value_end--;
          break;
        }
        value_end = line.find_first_of(" \t\n\r", value_end);
        /* consume any white space, incase there is another word.
         * any trailing whitespace will be removed shortly */
        value_end = line.find_first_not_of(" \t\n\r", value_end);
      } while (value_end != string::npos);
      /* strip any trailing space from value*/
      value_end = line.find_last_not_of(" \t\n\r", value_end);

      if (value_end >= start)
      {
        value = line.substr(start, value_end +1 - start);
      }
      else
      {
        /* error: no value */
        error_reporter.warn(where()) << "no value found\n";
        return false;
      }

      return true;
    }

    void CfgStreamParser::scanStream(istream& in)
    {
      do
      {
        linenum++;
        string line;
        getline(in, line);
        string option, value;
        if (scanLine(line, option, value))
        {
          /* store the value in option */
          storePair(true, false, option, value);
        }
      } while(!!in);
    }

    /* for all options in opts, set their storage to their specified
     * default value */
    void setDefaults(Options& opts)
    {
      for(Options::NamesPtrList::iterator it = opts.opt_list.begin(); it != opts.opt_list.end(); it++)
      {
        (*it)->opt->setDefault();
      }
    }

    void parseConfigFile(Options& opts, const string& filename, ErrorReporter& error_reporter)
    {
      ifstream cfgstream(filename.c_str(), ifstream::in);
      if (!cfgstream)
      {
        error_reporter.error(filename) << "Failed to open config file\n";
        return;
      }
      CfgStreamParser csp(filename, opts, error_reporter);
      csp.scanStream(cfgstream);
    }

// ---------- configuration update

    bool OptionUpdater::update(Options& opts, unsigned int target_id, ErrorReporter& error_reporter)
    {
      CfgStreamParser csp(name, opts, error_reporter);

      while (cmdstack.lower_bound(target_id) == cmdstack.end()) // store all until finding id >= target_id
      {
        // read file
        csp.linenum = ++linenum;
        string line;
        getline(in, line);
        if (!in)
          return false;
        string id_str, cmdline;
        if (csp.scanLine(line, id_str, cmdline))
        {
          /* convert id_str to id_num */
          int id = stoi(id_str);
          // TODO: deal with stoi() failure ?
          // store in cmdstack
          cmdstack[id] = cmdline;
        }
      }
      auto found = cmdstack.find(target_id);
      if (found != cmdstack.end())
      {
        // generate argc/argv for use by scanArgv
        string cmdline = found->second;
        cmdstack.erase(found);
        istringstream ss(cmdline);
        string arg;
        list<std::string> ls;
        vector<const char*> argv;
        argv.push_back(0);  // first dummy element (scanArgv starts parsing at index 1)
        while (ss >> arg)
        {
          ls.push_back(arg);
          argv.push_back(ls.back().c_str());
        }
        argv.push_back(0);  // terminating null pointer

        // parse cmdline
        scanArgv(opts, argv.size()-1, &argv[0], error_reporter);

        return true; // unless parse fails ?
      }
      return false;
    }

    bool OptionUpdater::openFile(const string& rName, ErrorReporter& error_reporter)
    {
      name = rName;
      in.open(rName.c_str());
      if (!in)
      {
        error_reporter.error(name) << "Failed to open update file\n";
        return false;
      }
      return true;
    }
  }
}

//! \}
