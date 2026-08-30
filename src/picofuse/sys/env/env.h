#pragma once

/** @brief Stores the process's argc/argv, for sys_env_args_parse() to
 * later read. Called once from sys_init(). */
void _sys_env_set_args(int argc, char *argv[]);
