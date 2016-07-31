#pragma once

//-----------------------------------------------------------------------------
struct ArgParse
{
  void AddFlag(const char* shortName, const char* longName, bool* out)
  {
    handlers.push_back(new FlagHandler(shortName, longName, out));
  }

  void AddIntArgument(const char* shortName, const char* longName, int* out)
  {
    handlers.push_back(new IntHandler(shortName, longName, out));
  }

  void AddFloatArgument(const char* shortName, const char* longName, float* out)
  {
    handlers.push_back(new FloatHandler(shortName, longName, out));
  }

  void AddStringArgument(const char* shortName, const char* longName, string* out)
  {
    handlers.push_back(new StringHandler(shortName, longName, out));
  }

  bool Parse(int argc, char** argv)
  {
    // parse all keyword arguments
    int idx = 0;
    while (idx < argc)
    {
      string cmd = argv[idx];
      // oh noes, O(n), tihi
      BaseHandler* handler = nullptr;
      for (int i = 0; i < (int)handlers.size(); ++i)
      {
        if (cmd == "--" + handlers[i]->longName || cmd == "-" + handlers[i]->shortName)
        {
          handler = handlers[i];
          break;
        }
      }

      if (!handler)
      {
        // end of keyword arguments, so copy the remainder into the positional list
        for (int i = idx; i < argc; ++i)
          positional.push_back(argv[i]);
        return true;
      }

      if (argc - idx < handler->RequiredArgs())
      {
        error = string("Too few arguments left for: ") + argv[idx];
        return false;
      }

      const char* arg = handler->RequiredArgs() ? argv[idx + 1] : nullptr;
      if (!handler->Process(arg))
      {
        error = string("Error processing argument: ") + (arg ? arg : "");
        return false;
      }

      idx += handler->RequiredArgs() + 1;
    }

    return true;
  }

  void PrintHelp() { printf("Valid arguments are:\n"); }

  struct BaseHandler
  {
    BaseHandler(const char* shortName, const char* longName)
      : shortName(shortName ? shortName : ""), longName(longName ? longName : "")
    {
    }
    virtual int RequiredArgs() = 0;
    virtual bool Process(const char* value) = 0;

    string shortName;
    string longName;
  };

  struct FlagHandler : public BaseHandler
  {
    FlagHandler(const char* shortName, const char* longName, bool* out)
      : BaseHandler(shortName, longName), out(out)
    {
    }
    virtual int RequiredArgs() override { return 0; }
    virtual bool Process(const char* value) override
    {
      *out = true;
      return true;
    }
    bool* out;
  };

  struct IntHandler : public BaseHandler
  {
    IntHandler(const char* shortName, const char* longName, int* out)
      : BaseHandler(shortName, longName), out(out)
    {
    }
    virtual int RequiredArgs() override { return 1; }
    virtual bool Process(const char* value) override { return sscanf(value, "%d", out) == 1; }
    int* out;
  };

  struct FloatHandler : public BaseHandler
  {
    FloatHandler(const char* shortName, const char* longName, float* out)
      : BaseHandler(shortName, longName), out(out)
    {
    }
    virtual int RequiredArgs() override { return 1; }
    virtual bool Process(const char* value) override { return sscanf(value, "%f", out) == 1; }
    float* out;
  };

  struct StringHandler : public BaseHandler
  {
    StringHandler(const char* shortName, const char* longName, string* out)
      : BaseHandler(shortName, longName), out(out)
    {
    }
    virtual int RequiredArgs() override { return 1; }
    virtual bool Process(const char* value) override
    {
      *out = value;
      return true;
    }
    string* out;
  };

  vector<string> positional;
  string error;
  vector<BaseHandler*> handlers;
};
