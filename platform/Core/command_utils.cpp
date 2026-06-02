#include "bg_editor_globals.h"

#include <cstdio>
#include <cstring>

int run_command_capture(const char *cmd, char *out, size_t outsz)
{
    if (out && outsz) out[0] = '\0';
    if (!cmd || !cmd[0]) return -1;
    char full[4096];
    snprintf(full, sizeof full, "%s 2>&1", cmd);
#ifdef _WIN32
    FILE *p = _popen(full, "r");
#else
    FILE *p = popen(full, "r");
#endif
    if (!p) {
        if (out && outsz) snprintf(out, outsz, "Could not start command.");
        return -1;
    }

    size_t len = 0;
    char buf[512];
    while (fgets(buf, sizeof buf, p)) {
        if (out && outsz && len + 1 < outsz) {
            size_t n = strlen(buf);
            if (n > outsz - len - 1) n = outsz - len - 1;
            memcpy(out + len, buf, n);
            len += n;
            out[len] = '\0';
        }
    }
#ifdef _WIN32
    return _pclose(p);
#else
    return pclose(p);
#endif
}
