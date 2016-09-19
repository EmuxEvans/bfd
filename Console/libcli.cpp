/*****************************************************************
* Copyright (C) 2016 Maipu Communication Technology Co.,Ltd.*
******************************************************************
* libcli.cpp
*
* DESCRIPTION:
*	ʵ���������нӿڵ�ע�ᡢ��������ȫ�ȹ��ܡ�
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
#include <winsock2.h>
#include <windows.h>

#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <ctype.h>

#include "libcli.h"
#include "bfd_cli.h"
#include "console.h"
#include "common.h"
# define UNUSED(d) d

#define INVALID_COMMAND		"\nInvalid Command"
#define INVALID_ARGUMENT	"\nInvalid argument @%s"

static char *cliCommandName(struct cli_def *cli, struct cli_command *command);
static CLI_ARGUMENT_STATE checkIPAddress(char *ip);
static CLI_ARGUMENT_STATE checkNumber(char *num);
static INT32 cliFindCommand(struct cli_def *cli, struct cli_command *commands, INT32 num_words, char *words[],
	INT32 start_word);
static INT32 vasprintf(char **strp, const char *fmt, va_list args);
static INT32 asprintf(char **strp, const char *fmt, ...);
static INT32 cliShowHelp(struct cli_def *cli, struct cli_command *c);
static INT32 cliInitHelp(struct cli_def *cli, UNUSED(const char *command), UNUSED(char *argv[]), UNUSED(INT32 argc));
static INT32 cliIntHistory(struct cli_def *cli, UNUSED(const char *command), UNUSED(char *argv[]), UNUSED(INT32 argc));
static INT32 cliParseLine(const char *line, char *words[], INT32 max_words);

/*****************************************************************
* DESCRIPTION:
*	������������
* INPUTS:
*	cli - �����нӿ�
*	command - ��Ҫ������ֵ�����ṹ��ָ��
* OUTPUTS:
*	outtime - ����ĵ�ǰʱ��
* RETURNS:
*	��������ֵ��ַ���
*****************************************************************/
static char *cliCommandName(struct cli_def *cli, struct cli_command *command)
{
	char *name = cli->commandname;
	char *o;

	if (name)
	{
		free(name);
	}
	if (!(name = (char*)calloc(1, 1)))
	{
		return NULL;
	}
	while (command)
	{
		o = name;
		if (asprintf(&name, "%s%s%s", command->command, *o ? " " : "", o) == -1)
		{
			fprintf(stderr, "Couldn't allocate memory for command_name: %s", strerror(errno));
			free(o);
			return NULL;
		}
		command = command->parent;
		free(o);
	}
	cli->commandname = name;
	return name;
}


/*****************************************************************
* DESCRIPTION:
*	���ip��ַ�Ƿ���ȷ
* INPUTS:
*	ip - ��Ҫ�����ַ���
* OUTPUTS:
*	none 
* RETURNS:
*	CLI_ARGUMENT_STATE_FALSE - ����ip��ַ
*	CLI_ARGUMENT_STATE_INCOMPLETE - ip��ַ������
*	CLI_ARGUMENT_STATE_TRUE - ��������ip��ַ
*****************************************************************/
static CLI_ARGUMENT_STATE checkIPAddress(char *ip)
{
	INT32 len = strlen(ip);

	/*255.255.255.255�15���ֽڼ�һ��'\0'���16�ֽ�*/
	if (len > 16)
	{
		return CLI_ARGUMENT_STATE_FALSE;
	}

	INT32 countOfNumber = 0;
	INT32 countOfDot = 0;

	for (INT32 i = 0; i < len; i++)
	{
		if ((ip[i] >= '0') && (ip[i] <= '9'))
		{
			countOfNumber++;
			if (countOfNumber == 3)
			{
				/*�����3λ��,����Ƿ�С��255*/
				INT32 num = (ip[i - 2] - '0') * 100 + (ip[i - 1] - '0') * 10 + (ip[i] - '0');
				if (num > 255)
				{
					return CLI_ARGUMENT_STATE_FALSE;
				}
			}
			else if (countOfNumber > 3)
			{
				/*���ĳһ�����ִ���3λ*/
				return CLI_ARGUMENT_STATE_FALSE;
			}
		}
		else if (ip[i] == '.')
		{
			/*���������������'.',������'.'��ͷ*/
			if (countOfNumber == 0)
			{
				return CLI_ARGUMENT_STATE_FALSE;
			}
			else
			{
				countOfDot++;
				countOfNumber = 0;
			}
		}
		else
		{
			/*������������ַ�*/
			return CLI_ARGUMENT_STATE_FALSE;
		}
	}

	/*�ڱ����������ַ����Ժ�*/
	if (countOfDot > 3)
	{
		/*����д���3��'.'*/
		return CLI_ARGUMENT_STATE_FALSE;
	}
	else if ((countOfDot < 3) || (countOfDot == 3 && countOfNumber == 0))
	{
		/*���С��3��'.',�������һλΪ'.',˵��������*/
		return CLI_ARGUMENT_STATE_INCOMPLETE;
	}

	/*���'.'����3��*/
	return CLI_ARGUMENT_STATE_TRUE;
}

/*****************************************************************
* DESCRIPTION:
*	����ַ����Ƿ�Ϊ����
* INPUTS:
*	num - ��Ҫ���������ַ���
* OUTPUTS:
*	none
* RETURNS:
*	CLI_ARGUMENT_STATE_FALSE - ��������
*	CLI_ARGUMENT_STATE_TRUE - ������
*****************************************************************/
static CLI_ARGUMENT_STATE checkNumber(char *num)
{
	INT32 len = strlen(num);
	while (len--)
	{
		if (num[len]<'0' || num[len]>'9')
		{
			return CLI_ARGUMENT_STATE_FALSE;
		}
	}
	return CLI_ARGUMENT_STATE_TRUE;
}

/*****************************************************************
* DESCRIPTION:
*	����������������һ����ά�����ַ�������󣬵�������ÿ���ֶΣ����ҵ������
*	����ṹ��
* INPUTS:
*	cli - �����нӿ�
*	commands - ��ʼ������ṹ��
*	num_words - �ֶ���Ŀ
*	words[] - �ֶε�ָ������
* OUTPUTS:
*	none
* RETURNS:
*	TRUE - �����ɹ������ɹ�ִ����������
*	FALSE - �����д���
*****************************************************************/
static INT32 cliFindCommand(struct cli_def *cli, struct cli_command *commands, INT32 num_words, char *words[],
	INT32 start_word)
{
	/*start_word �Ǵ�0��ʼ��*/
	struct cli_command *c, *again_config = NULL, *again_any = NULL;
	INT32 c_words = num_words;

	if ((words[start_word] == NULL) || (commands == NULL) || (cli == NULL) || ((num_words - 1) < start_word) || ((num_words - 1) < start_word))
	{
		printf_s("\n������������:");
		printf_s("\n &words[start_word]=%d", (INT32)words[start_word]);
		printf_s("\n &commands=%d", (INT32)commands);
		printf_s("\n &cli=%d", (INT32)cli);
		printf_s("\n num_words=%d,start_word=%d", num_words, start_word);
		system("pause");
		exit(42);
	}

	/*������������'?',�������ڽ�������������*/
	INT32 cmdLen = strlen(words[start_word]);
	/*�������������� '?' ��ʾ�������*/
	if (words[start_word][cmdLen - 1] == '?')
	{
		/*���ֻ��һ�� '?',��ʾ�������е��������*/
		BOOL findCmd = FALSE;
		for (c = commands; c; c = c->next)
		{
			if (_strnicmp(c->command, words[start_word], cmdLen - 1) == 0
				&& (c->callback || c->children))
			{
				printf_s("\n  %-20s %s", c->command, (c->help != NULL ? c->help : ""));
				findCmd = TRUE;
			}
		}
		if (findCmd == TRUE)
		{
			return CLI_QUIT;
		}
		/*���������û���˳�,��˵��������������޷�ʶ���,*/
		printf_s(INVALID_COMMAND);
		return CLI_ERROR;
	}

	for (c = commands; c; c = c->next)
	{
		/*�ȱȽ���������ƥ�䳤��*/
		if (_strnicmp(c->command, words[start_word], c->unique_len))
		{
			continue;
		}
		/*��������ƥ�䳤����ͬ,��ƥ�������ַ������ȵ��ַ�*/
		if (_strnicmp(c->command, words[start_word], strlen(words[start_word])))
		{
			continue;
		}
		/*�ҵ���������*/
		INT32 rc = CLI_OK;
		/*��������������滹��������,�����ж����������Ƿ���Ҫ����,�ٽ�������Ĳ���*/
		if (start_word == c_words - 1)
		{
			/*�����ǰ�����������һ������,������Ҫ����*/
			if (c->argType != CLI_ARGUMENT_TYPE_NONE)
			{
				printf_s("\nCommand need argument @%s", cliCommandName(cli, c));
				return CLI_ERROR;
			}
			else
			{
				/*û���������*/
				if (c->callback != NULL)
				{
					rc = c->callback(cli, cliCommandName(cli, c), NULL, 0);
				}
				return rc;
			}
		}

		INT32 commandOffset = 1;
		if (c->argType == CLI_ARGUMENT_TYPE_NONE)
		{
			commandOffset = 0;
		}

		/*������һ������ǰ,����Ӧ�ü�鵱ǰ����Ĳ����Ƿ���ȷ*/
		if (!(((num_words - 1) == (start_word + 1)) && words[start_word + 1][strlen(words[start_word + 1]) - 1] == '?'))
		{
			enum CLI_ARGUMENT_STATE argState = checkArgument(c, *(words + start_word + 1));
			if (argState == CLI_ARGUMENT_STATE_FALSE)
			{
				printf_s(INVALID_ARGUMENT, cliCommandName(cli, c));
				return CLI_ERROR;
			}
			else if (argState == CLI_ARGUMENT_STATE_INCOMPLETE)
			{
				printf_s("\nIncomplete argument @%s", cliCommandName(cli, c));
				return CLI_ERROR;
			}
			else if (argState == CLI_ARGUMENT_STATE_TRUE)
			{
				rc = CLI_OK;
			}
		}
		/*������������в���,���ҵ�ǰ�����ǵ����ڶ���,�����������һλ�Ƿ�Ϊ'?'*/
		if ((commandOffset == 1) && ((num_words - 1) == (start_word + 1)))
		{
			INT32 argLen = strlen(words[start_word + 1]);
			/*����ǲ��������� '?' ��ʾ��������*/
			if (words[start_word + 1][argLen - 1] == '?')
			{
				/*�������ֻ��һ��'?',����ǰ���Ƿ��������Ҫ��*/
				if (argLen != 1)
				{
					char *argcheck = (char *)malloc(argLen);
					if (argcheck == NULL)
					{
						printf_s("\nInternal error");
					}
					memcpy(argcheck, words[start_word + 1], argLen);
					argcheck[argLen - 1] = '\0';
					if (checkArgument(c, argcheck) == CLI_ARGUMENT_STATE_FALSE)
					{
						printf_s("\nIncomplete argument @%s", cliCommandName(cli, c));
						return CLI_ERROR;
					}
					free(argcheck);
				}
				//printf_s("\n  %-20s %s", c->command, (c->help != NULL ? c->help : ""));
				if (c->argHelp != NULL)
				{
					printf_s("\n   %s", c->argHelp);
				}

				return CLI_QUIT;
			}
		}

		/*��������ĸ������ڱ�������+1(����),�����������һ������,�����������û�в���,�������һ��,�������һ��*/
		if ((num_words - 1) > (start_word + commandOffset))
		{
			if (c->children == NULL)
			{
				/*���û��������,�������'?',��ʾclear���߲���ȷ������*/

				INT32 offset = (c->argType == CLI_ARGUMENT_TYPE_NONE) ? (0) : (1);
				INT32 argLen = strlen(words[start_word + 1 + offset]);
				if (words[start_word + 1 + offset][argLen - 1] == '?')
				{
					/*�������ֻ��һ��'?',����ǰ���Ƿ��������Ҫ��*/
					if (argLen != 1)
					{
						printf_s(INVALID_COMMAND);
					}
					else
					{
						printf_s("\n  <clear>");
					}
					return CLI_QUIT;
				}
				else
				{
					printf_s(INVALID_ARGUMENT, cliCommandName(cli, c));
					return CLI_ERROR;
				}
			}
			/*+1+1����������,������һ������*/
			rc = cliFindCommand(cli, c->children, num_words, words, start_word + 1 + commandOffset);
		}

		if (rc == CLI_OK)
		{
			/*�����һ�����ʼ����,�ȷ���ֵ��ִ��callbac,��ֹ���һ���������,����ǰ���callback��ִ����*/
			if (c->callback != NULL)
			{
				if (c->argType == CLI_ARGUMENT_TYPE_NONE)
				{
					rc = c->callback(cli, cliCommandName(cli, c), NULL, 0);
				}
				else
				{
					rc = c->callback(cli, cliCommandName(cli, c), words + start_word + 1, 1);
				}
			}
		}
		return rc;
	}
	printf_s(INVALID_COMMAND);
	return CLI_ERROR;
}

/*****************************************************************
* DESCRIPTION:
*	�ɱ䳤�ȵ��ַ�����ʽ�������س��ȣ��ַ��������malloc����Ķ���
* INPUTS:
*	fmt - �����ʽ��%d %s %f ����)
*	args - �ɵ��ú����ṩ�Ŀɱ�����б�
* OUTPUTS:
*	strp - ����ַ���ָ��
* RETURNS:
*	�ַ�������
*****************************************************************/
static INT32 vasprintf(char **strp, const char *fmt, va_list args)
{
	INT32 size;

	size = vsnprintf(NULL, 0, fmt, args);
	if ((*strp = (char*)malloc(size + 1)) == NULL)
	{
		return -1;
	}

	size = vsnprintf(*strp, size + 1, fmt, args);
	return size;
}

/*****************************************************************
* DESCRIPTION:
*	���ݸ�ʽ�����ַ������ȣ������㹻���ڴ�ռ䣬���س��ȣ��ַ������
*	��malloc����Ķ����ǿsprintf
* INPUTS:
*	fmt - �����ʽ��%d %s %f ����)
*	... - ��Ӧ��ʽ���������
* OUTPUTS:
*	strp - ����ַ���ָ��
* RETURNS:
*	�ַ�������
*****************************************************************/
static INT32 asprintf(char **strp, const char *fmt, ...)
{
	va_list args;
	INT32 size;

	va_start(args, fmt);
	size = vasprintf(strp, fmt, args);

	va_end(args);
	return size;
}

/*****************************************************************
* DESCRIPTION:
*	������ʾһ������İ���
* INPUTS:
*	cli - �����нӿ�
*	c - Ҫ��ʾ�������������ĸ��ڵ�
* OUTPUTS:
*	none
* RETURNS:
*	CLI_OK - ��ʾ�ɹ�
*****************************************************************/
static INT32 cliShowHelp(struct cli_def *cli, struct cli_command *c)
{
	struct cli_command *p;

	for (p = c; p; p = p->next)
	{
		if (p->command)
		{
			printf_s("\n  %-20s %s", cliCommandName(cli, p), (p->help != NULL ? p->help : ""));
		}
	}

	return CLI_OK;
}

/*****************************************************************
* DESCRIPTION:
*	������ʾһ������İ�����"help"����Ļص�����
* INPUTS:
*	cli - �����нӿ�
*	�������δʹ��
* OUTPUTS:
*	none
* RETURNS:
*	CLI_OK - ��ʾ�ɹ�
*****************************************************************/
static INT32 cliInitHelp(struct cli_def *cli, UNUSED(const char *command), UNUSED(char *argv[]), UNUSED(INT32 argc))
{
	if (argc != 0)
	{
		printf_s(INVALID_COMMAND);
		return CLI_ERROR;
	}
	printf_s("\nCommands available:");
	cliShowHelp(cli, cli->commands);
	return CLI_OK;
}

/*****************************************************************
* DESCRIPTION:
*	��ʾ��������������Ӧ"history"�Ļص�����
* INPUTS:
*	cli - �����нӿ�
*	�������δʹ��
* OUTPUTS:
*	none
* RETURNS:
*	CLI_OK - ��ʾ�ɹ�
*****************************************************************/
static INT32 cliIntHistory(struct cli_def *cli, UNUSED(const char *command), UNUSED(char *argv[]), UNUSED(INT32 argc))
{
	INT32 i;

	printf_s("\nCommand history:");
	for (i = 0; i < MAX_HISTORY; i++)
	{
		if (cli->history[i])
		{
			printf_s("\n%3d. %s", i, cli->history[i]);
		}
	}

	return CLI_OK;
}

/*****************************************************************
* DESCRIPTION:
*	����һ����������ÿ���ֶΣ�ֻ֧�ֿո���룬
* INPUTS:
*	cli - �����нӿ�
*	�������δʹ��
* OUTPUTS:
*	none
* RETURNS:
*	CLI_OK - ��ʾ�ɹ�
*****************************************************************/
static INT32 cliParseLine(const char *line, char *words[], INT32 max_words)
{
	INT32 nwords = 0;
	const char *p = line;
	const char *word_start = 0;
	INT32 inquote = 0;

	while (*p)
	{
		if (!isspace(*p))
		{
			word_start = p;
			break;
		}
		p++;
	}

	while (nwords < max_words - 1)
	{
		if (!*p || *p == inquote || (word_start && !inquote && (isspace(*p) || *p == '|')))
		{
			if (word_start)
			{
				INT32 len = p - word_start;
				words[nwords] = (char*)malloc(len + 1);
				memcpy(words[nwords], word_start, len);
				words[nwords++][len] = '\0';
			}

			if (!*p)
			{
				break;
			}
			if (inquote)
			{
				p++; /* skip over trailing quote */
			}
			inquote = 0;
			word_start = 0;
		}
		else if (*p == '"' || *p == '\'')
		{
			inquote = *p++;
			word_start = p;
		}
		else
		{
			if (!word_start)
			{
				if (*p == '|')
				{
					if (!(words[nwords++] = _strdup("|")))
						return 0;
				}
				else if (!isspace(*p))
				{
					word_start = p;
				}
			}

			p++;
		}
	}

	return nwords;
}

/*****************************************************************
* DESCRIPTION:
*	ע��һ������
* INPUTS:
*	cli - �����нӿ�ָ��
*	parent - ��������ĸ�����
*	command - ��Ҫע��������ȫ��
*	callback - ����Ļص�������ΪNULL��ʾ���������ִ��
*	help - ��������İ�����Ϣ���ַ�����ʽ
*	argType - ��������
* OUTPUTS:
*	none
* RETURNS:
*	�����������ɵ�struct cli_commandָ��
*****************************************************************/
__declspec(dllexport) struct cli_command *cliRegisterCommand(struct cli_def *cli, struct cli_command *parent, const char *command,
	INT32(*callback)(struct cli_def *, const char *, char **, INT32), const char *help, CLI_ARGUMENT_TYPE argType, const char *argHelp)
{
	struct cli_command *c, *p;

	if (!command)
	{
		return NULL;
	}
	if (!(c = (cli_command*)calloc(sizeof(struct cli_command), 1)))
	{
		return NULL;
	}
	c->callback = callback;
	c->next = NULL;
	if (!(c->command = _strdup(command)))
	{
		return NULL;
	}
	c->parent = parent;
	if (help && !(c->help = _strdup(help)))
	{
		return NULL;
	}
	c->argType = argType;
	c->argHelp = _strdup(argHelp);
	if ((c->argType != CLI_ARGUMENT_TYPE_NONE) && c->argHelp == NULL)
	{
		return NULL;
	}

	if (parent)
	{
		if (!parent->children)
		{
			parent->children = c;
		}
		else
		{
			for (p = parent->children; p && p->next; p = p->next);
			if (p) p->next = c;
		}
	}
	else
	{
		if (!cli->commands)
		{
			cli->commands = c;
		}
		else
		{
			for (p = cli->commands; p && p->next; p = p->next);

			if (p)
			{
				p->next = c;
			}
		}
	}
	return c;
}

/*****************************************************************
* DESCRIPTION:
*	�����Ѿ�ע������Ϊÿ�������һ�����Ψһƥ���ַ���
* INPUTS:
*	cli - �����нӿ�ָ��
*	commands - �������ĸ��ڵ�
* OUTPUTS:
*	none
* RETURNS:
*	CLI_OK - �����ɹ�
*****************************************************************/
__declspec(dllexport) INT32 cliBuildShortest(struct cli_def *cli, struct cli_command *commands)
{
	struct cli_command *c, *p;
	char *cp, *pp;
	UINT32 len;

	for (c = commands; c; c = c->next)
	{
		c->unique_len = strlen(c->command);

		c->unique_len = 1;
		for (p = commands; p; p = p->next)
		{
			if (c == p)
				continue;

			cp = c->command;
			pp = p->command;
			len = 1;

			while (*cp && *pp && *cp++ == *pp++)
			{
				len++;
			}
			if (len > c->unique_len)
			{
				c->unique_len = len;
			}
		}

		if (c->children)
		{
			cliBuildShortest(cli, c->children);
		}
	}

	return CLI_OK;
}

/*****************************************************************
* DESCRIPTION:
*	�����нӿڵĴ���
* INPUTS:
*	none
* OUTPUTS:
*	none
* RETURNS:
*	����������ӿ�ָ��
*****************************************************************/
__declspec(dllexport) struct cli_def *cliInit(void)
{
	struct cli_def *cli;

	if (!(cli = (cli_def*)calloc(sizeof(struct cli_def), 1)))
	{
		return 0;
	}
	cli->buf_size = 1024;
	if (!(cli->buffer = (char*)calloc(cli->buf_size, 1)))
	{
		free_z(cli);
		return 0;
	}
	cliRegisterCommand(cli, 0, "help", cliInitHelp, "Show available commands", CLI_ARGUMENT_TYPE_NONE, NULL);
	cliRegisterCommand(cli, 0, "history", cliIntHistory,
		"Show a list of previously run commands", CLI_ARGUMENT_TYPE_NONE, NULL);
	return cli;
}

/*****************************************************************
* DESCRIPTION:
*	����һ����ʷ���һ���ڰ��»س����������ʱ�����
* INPUTS:
*	cli - �����нӿڵ�ָ��
*	cmd - ���ӵ���ʷ����
* OUTPUTS:
*	none
* RETURNS:
*	CLI_ERROR - ������ʷ�������ﵽ���
*	CLI_OK - ����������ʷ�ɹ�
*****************************************************************/
__declspec(dllexport) INT32 cliAddHistory(struct cli_def *cli, const char *cmd)
{
	INT32 i;
	for (i = 0; i < MAX_HISTORY; i++)
	{
		if (!cli->history[i])
		{
			// if (i == 0 || _stricmp(cli->history[i-1], cmd))
			if ((i != 0) && (_stricmp(cli->history[i - 1], cmd) == 0))
			{
				/*������ϴ�ִ�е�������ͬ,����Error*/
				return CLI_REPEAT;
			}
			if (!(cli->history[i] = _strdup(cmd)))
			{
				return CLI_ERROR;
			}
			return CLI_OK;
		}
	}
	// No space found, drop one off the beginning of the list
	free(cli->history[0]);
	for (i = 0; i < MAX_HISTORY - 1; i++)
	{
		cli->history[i] = cli->history[i + 1];
	}
	if (!(cli->history[MAX_HISTORY - 1] = _strdup(cmd)))
	{
		return CLI_ERROR;
	}
	return CLI_OK;
}

/*****************************************************************
* DESCRIPTION:
*	����������һ������
* INPUTS:
*	cli - �����нӿڵ�ָ��
*	command - ���������
* OUTPUTS:
*	none
* RETURNS:
*	CLI_ERROR - ����ʧ��
*	CLI_OK - ���гɹ�
*****************************************************************/
__declspec(dllexport) INT32 cliRunCommand(struct cli_def *cli, const char *command)
{
	INT32 r;
	UINT32 num_words, i;
	char *words[CLI_MAX_LINE_WORDS] = { 0 };

	if (!command)
	{
		return CLI_ERROR;
	}
	while (isspace(*command))
	{
		command++;
	}
	if (!*command)
	{
		return CLI_OK;
	}
	num_words = cliParseLine(command, words, CLI_MAX_LINE_WORDS);

	if (num_words)
	{
		r = cliFindCommand(cli, cli->commands, num_words, words, 0);
	}
	else
	{
		r = CLI_ERROR;
	}
	for (i = 0; i < num_words; i++)
	{
		free(words[i]);
	}
	if (r == CLI_QUIT)
	{
		return r;
	}
	return CLI_OK;
}

/*****************************************************************
* DESCRIPTION:
*	������������ַ�����ȫ����
* INPUTS:
*	cli - �����нӿڵ�ָ��
*	command - ������Ĳ����ַ�
*	completions - ��ȫ���ַ��������ָ��
*	max_completions - ���ȫ����
* OUTPUTS:
*	none
* RETURNS:
*	��ȫ����������
*****************************************************************/
__declspec(dllexport) INT32 cliGetCompletions(struct cli_def *cli, const char *command, char **completions, INT32 max_completions)
{
	struct cli_command *c;
	struct cli_command *n;
	INT32 num_words, save_words, i, k = 0;
	char *words[CLI_MAX_LINE_WORDS] = { 0 };

	if (!command)
	{
		return 0;
	}
	while (isspace(*command))
	{
		command++;
	}

	save_words = num_words = cliParseLine(command, words, sizeof(words) / sizeof(words[0]));
	if (!command[0] || command[strlen(command) - 1] == ' ')
	{
		num_words++;
	}

	if (num_words != 0)
	{
		for (c = cli->commands, i = 0; c && i < num_words && k < max_completions; c = n)
		{
			n = c->next;
			if (words[i] && _strnicmp(c->command, words[i], strlen(words[i])))
			{
				continue;
			}
			if (i < num_words - 1)
			{
				if (strlen(words[i]) < c->unique_len)
				{
					continue;
				}
				if ((i + 1) > (num_words - 1))
				{
					/*���û���������*/
					return 0;
				}
				if (checkArgument(c, words[i + 1]) != CLI_ARGUMENT_STATE_TRUE)
				{
					/*���ǰ��Ĳ�������ȷ*/
					return 0;
				}
				if (c->argType != CLI_ARGUMENT_TYPE_NONE)
				{
					i++;
				}
				i++;
				n = c->children;
				continue;
			}
			completions[k++] = c->command;
		}
	}
	for (i = 0; i < save_words; i++)
	{
		free(words[i]);
	}
	return k;
}

/*****************************************************************
* DESCRIPTION:
*	�������Ƿ���ȷ������
* INPUTS:
*	c - ��Ҫ������������ָ��
*	cmd - Ҫ���Ĳ������ַ���
* OUTPUTS:
*	none
* RETURNS:
*	CLI_ARGUMENT_STATE_TRUE - ������ȷ
*	CLI_ARGUMENT_STATE_FALSE - ��������
*	CLI_ARGUMENT_STATE_INCOMPLETE - ����������
*****************************************************************/
__declspec(dllexport) CLI_ARGUMENT_STATE checkArgument(cli_command *c, char *arg)
{
	CLI_ARGUMENT_STATE ret = CLI_ARGUMENT_STATE_TRUE;
	if (c->argType == CLI_ARGUMENT_TYPE_NONE)
	{
		return CLI_ARGUMENT_STATE_TRUE;
	}
	if (arg == NULL)
	{
		return CLI_ARGUMENT_STATE_FALSE;
	}
	if (c->argType == CLI_ARGUMENT_TYPE_IP)
	{
		ret = checkIPAddress(arg);
	}
	else if (c->argType == CLI_ARGUMENT_TYPE_NUMBER)
	{
		ret = checkNumber(arg);
	}
	else if (c->argType == CLI_ARGUMENT_TYPE_STRING)
	{
		/*������Ҫ����ַ��������Ƿ���ȷ*/
		//argState = checkString(*(words + start_word + 1));
	}
	return ret;
}


/*****************************************************************
* DESCRIPTION:
*	��Ӧ����������ʾ������ʷ
* INPUTS:
*	cli - Ҫ��ʾ��ʷ�������нӿ�
*	c - �������룬�����ߡ�
*	in_history - ������������ʷ�Ļ�����ָ��
*	numOfHistory - ��ĿǰΪֹ�������������
* OUTPUTS:
*	none
* RETURNS:
*	��ʷ����ַ�����ʽ
*****************************************************************/
__declspec(dllexport) char* cliHistory(struct cli_def *cli, char c, INT32 *in_history, INT32 numOfHistory)
{
	if (c == 0x48) // Up
	{
		(*in_history)--;
		if (*in_history < 0)
		{
			*in_history = 0;
		}
		if (cli->history[*in_history] != NULL)
		{
			return cli->history[*in_history];
		}
		else
		{
			return NULL;
		}
	}
	else
	{
		(*in_history)++;
		if (*in_history > (numOfHistory - 1))
		{
			*in_history = numOfHistory - 1;
			return NULL;
		}
		else
		{
			if (cli->history[*in_history] != NULL)
			{
				return cli->history[*in_history];
			}
		}
	}
	return NULL;
}
