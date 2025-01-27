template <typename EF> class scope_exit
{
    EF exit_func;

  public:
    scope_exit(EF &&exit_func) : exit_func{exit_func} {};
    ~scope_exit()
    {
        exit_func();
    }
};

#define SCOPE_EXIT(x, action, ...) scope_exit x##_se([x, __VA_ARGS__] action);
