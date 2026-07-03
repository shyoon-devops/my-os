#ifndef MY_OS_USER_MODE_H
#define MY_OS_USER_MODE_H

#include "types.h"

void user_mode_init(void);

/*
 * path의 user ELF를 현재 task context에서 로드하고 ring3로 실행한다.
 * process 모듈이 user-elf task 안에서 호출한다.
 * 반환: 0 = 정상 종료(*out_exit_code 유효), -1 = 로드/실행 실패
 */
s32 user_mode_run_path(const char* path, u64* out_exit_code);

u64 user_mode_stack_bottom(void);
u64 user_mode_stack_top(void);
u64 user_mode_entry_address(void);
u64 user_mode_blob_start(void);
u64 user_mode_blob_end(void);

void user_mode_register_builtin_commands(void);

#endif
