#ifndef WYRMXX_EXCEPT_H_
#define WYRMXX_EXCEPT_H_

#include <stdexcept>
#include <wyrm.h>

namespace wyrmxx
{
    class except_base : public std::runtime_error
    {
    public:
        except_base(wyrm_error error, const char* ext = "")
            : std::runtime_error(ext)
            , error_{error}
        {
        }

        wyrm_error get_error() const { return error_; }

    private:
        wyrm_error error_;
    };

    class out_of_memory : public except_base
    {
    public:
        explicit out_of_memory(const char* ext = "") : except_base{WYRM_ERR_NOMEM, ext} {}
    };

    inline void check_wyrm_error(wyrm_error err, const char* ext = "")
    {
        switch (err) {
        case WYRM_ERR_NOMEM:
            throw out_of_memory(ext);

        case WYRM_ERR_NONE:
            return;

        default:
            throw except_base{err, ext};
        }
    }

}

#endif
