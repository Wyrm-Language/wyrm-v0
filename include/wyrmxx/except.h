#ifndef WYRMXX_EXCEPT_H_
#define WYRMXX_EXCEPT_H_

#include <stdexcept>
#include <wyrm.h>

namespace wyrmxx
{
    class except_base : public std::runtime_error
    {
    public:
        except_base(wy_error error, const char* ext = "")
            : std::runtime_error(ext)
            , error_{error}
        {
        }

        wy_error get_error() const { return error_; }

    private:
        wy_error error_;
    };

    class out_of_memory : public except_base
    {
    public:
        explicit out_of_memory(const char* ext = "") : except_base{WY_ERR_NOMEM, ext} {}
    };

    inline void check_wy_error(wy_error err, const char* ext = "")
    {
        switch (err) {
        case WY_ERR_NOMEM:
            throw out_of_memory(ext);

        case WY_ERR_NONE:
            return;

        default:
            throw except_base{err, ext};
        }
    }

}

#endif
