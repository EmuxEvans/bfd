/*****************************************************************
* Copyright (C) 2016 Maipu Communication Technology Co.,Ltd.*
******************************************************************
* bfd_cli.h
*
* DESCRIPTION:
*	�����˻���cli��bfd�Ľӿڡ�
* AUTHOR:
*	̷���
* CREATED DATE:
*	2016 �� 5 �� 15 ��
* REVISION:
*	1.0
*
* MODIFICATION HISTORY
* --------------------
* $Log:$
*
*****************************************************************/
#ifndef _BFD_CLI_H_
#define _BFD_CLI_H_

#include "windows.h"

enum CLI_ARGUMENT_STATE
{
	CLI_ARGUMENT_STATE_FALSE=0,
	CLI_ARGUMENT_STATE_TRUE,
	CLI_ARGUMENT_STATE_INCOMPLETE,
};


INT32 cliBfdSetMultiplier(struct cli_def *cli, const char *command, char *argv[], int argc);
INT32 cliBfdSetTxInterval(struct cli_def *cli, const char *command, char *argv[], int argc);
INT32 cliBfdSetRxInterval(struct cli_def *cli, const char *command, char *argv[], int argc);
INT32 cliBfdCreateSession(struct cli_def *cli, const char *command, char *argv[], int argc);
INT32 cliBfdDeleteSession(struct cli_def *cli, const char *command, char *argv[], int argc);
INT32 cliBfdShowSession(struct cli_def *cli, const char *command, char *argv[], int argc);
#endif