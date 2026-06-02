#ifndef APP_DIAGNOSTICS_H
#define APP_DIAGNOSTICS_H

#ifdef __cplusplus
extern "C" {
#endif

void bdd_diag_init(int argc, char **argv);
void bdd_diag_write(const char *msg);
void bdd_save_logf(const char *fmt, ...);
const char *bdd_last_save_error(void);
void bdd_clear_last_save_error(void);

#ifdef __cplusplus
}
#endif

#endif
