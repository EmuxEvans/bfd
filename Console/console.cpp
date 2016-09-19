/*****************************************************************
* Copyright (C) 2016 Maipu Communication Technology Co.,Ltd.*
******************************************************************
* console.cpp
*
* DESCRIPTION:
*	ʵ�����û����棬��Ӧ���̰�����
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
#include "stdio.h"
#include "windows.h"
#include "conio.h"
#include "console.h"
#include "libcli.h"
#include "bfd_cli.h"
#include "libbfd.h"
#include "common.h"


static void Key_Left(void);
static void Key_Right(INT32 inputBufferTail);
static void refreshLine(char *buffer, INT32 cursorPos);
static void newLine(char *buf, INT32 *tail);
static INT32 socketInit(void);

/*****************************************************************
* DESCRIPTION:
*	��ʼ������ϵͳ��ע��һЩ��Ĭ�ϵ����Ȼ��ֻ��getchѭ����ȡ��������
* INPUTS:
*	none
* OUTPUTS:
*	none
* RETURNS:
*	���̽�������
*****************************************************************/
int main(void)
{
	char inputBuffer[INPUT_BUFFER_MAX_CHAR];
	CONSOLE_CURSOR_INFO cci;         //��������Ϣ�ṹ��  
	CONSOLE_SCREEN_BUFFER_INFO bInfo;
	HANDLE handle_out = GetStdHandle(STD_OUTPUT_HANDLE);
	cci.bVisible = TRUE;
	cci.dwSize = CURSOR_SIZE;
	SetConsoleCursorInfo(handle_out, &cci);
	SetConsoleTitle(L"BFD����");

	socketInit();
	HANDLE tem = bfdInit();
	CloseHandle(tem);

	struct cli_def *cli = cliInit();
	struct cli_command *com = NULL;
	char argHelp[INPUT_BUFFER_MAX_CHAR];
	memset(argHelp, '\0', sizeof(argHelp));

	com = cliRegisterCommand(cli, NULL, "bfd", NULL, "BFD configuration commands", CLI_ARGUMENT_TYPE_NONE, NULL);
	com = cliRegisterCommand(cli, com, "session", cliBfdCreateSession, "BFD Session configuration commands", CLI_ARGUMENT_TYPE_IP, \
		"<A.B.C.D>	Destination IP Address of BFD Session");
	sprintf_s(argHelp, "<%d-%d>		Milliseconds", MIN_RX_INTERVAL / 1000, MAX_RX_INTERVAL / 1000);
	com = cliRegisterCommand(cli, com, "min-receive-interval", cliBfdSetRxInterval, "    Minimum receive interval capability", CLI_ARGUMENT_TYPE_NUMBER, \
		argHelp);
	sprintf_s(argHelp, "<%d-%d>		Milliseconds", MIN_TX_INTERVAL / 1000, MAX_TX_INTERVAL / 1000);
	com = cliRegisterCommand(cli, com, "min-transmit-interval", cliBfdSetTxInterval, "    Transmit interval between BFD packets", CLI_ARGUMENT_TYPE_NUMBER, \
		argHelp);
	sprintf_s(argHelp, "<%d-%d>		value used to multiply the interval", MIN_MULT, MAX_MULT);
	cliRegisterCommand(cli, com, "multiplier", cliBfdSetMultiplier, " Multiplier value used to compute holddown", CLI_ARGUMENT_TYPE_NUMBER, \
		argHelp);

	com = cliRegisterCommand(cli, NULL, "no", NULL, "Negate a command or set its defaults", CLI_ARGUMENT_TYPE_NONE, NULL);
	com = cliRegisterCommand(cli, com, "bfd", NULL, "BFD configuration commands", CLI_ARGUMENT_TYPE_NONE, NULL);
	cliRegisterCommand(cli, com, "session", cliBfdDeleteSession, "BFD Session configuration commands", CLI_ARGUMENT_TYPE_IP, "<A.B.C.D>	Destination IP Address of BFD Session");

	com = cliRegisterCommand(cli, NULL, "show", NULL, "Show running system information", CLI_ARGUMENT_TYPE_NONE, NULL);
	com = cliRegisterCommand(cli, com, "bfd", NULL, "BFD configuration commands", CLI_ARGUMENT_TYPE_NONE, NULL);
	cliRegisterCommand(cli, com, "session", cliBfdShowSession, "BFD protocol info", CLI_ARGUMENT_TYPE_NONE, NULL);
	cliBuildShortest(cli, cli->commands);

	memset(inputBuffer, '\0', sizeof(inputBuffer));
	inputBuffer[0] = '>';
	printf_s(">");

	INT32 inputBufferTail = 1;
	INT32 historyPos = 0;
	INT32 NumberOfHistory = 0;
	char c = 0;
	char lastChar = 0;

	/*������*/
	//char test[] = "bfd s 192.168.80.100 m 500 m 500 m 40";
	//cliRunCommand(cli, test);
	
	while (1)
	{
		lastChar = c;
		c = _getch();
		if ((c >= '0'&&c <= '9') || (c >= 'a'&&c <= 'z') || (c >= 'A'&&c <= 'Z') || (c == ' ')\
			|| (c == '/') || (c == '\\') || (c == '.'))
		{
			/*�ַ�*/
			GetConsoleScreenBufferInfo(handle_out, &bInfo);
			if ((inputBufferTail + 1) >= INPUT_BUFFER_MAX_CHAR)
			{
				newLine(inputBuffer, &inputBufferTail);
				continue;
			}
			if (bInfo.dwCursorPosition.X == (inputBufferTail))
			{
				inputBuffer[bInfo.dwCursorPosition.X] = c;
				printf_s("%c", c);
			}
			else
			{
				for (INT32 i = inputBufferTail; i > (bInfo.dwCursorPosition.X - 1); i--)
				{
					inputBuffer[i] = inputBuffer[i - 1];
				}
				inputBuffer[bInfo.dwCursorPosition.X] = c;
				refreshLine(inputBuffer, bInfo.dwCursorPosition.X + 1);
			}
			inputBufferTail++;
		}
		else if (c == '\t')
		{
			/*TAB completion*/
			char *completions[CLI_MAX_LINE_WORDS];
			INT32 num_completions = 0;

			num_completions = cliGetCompletions(cli, &inputBuffer[1], completions, CLI_MAX_LINE_WORDS);
			if (num_completions == 0)
			{
				printf_s("\a");
			}
			else if (num_completions == 1)
			{
				//Single completion
				if (inputBufferTail == 1)
				{
					continue;
				}
				INT32 charNumOfWord = 0;
				INT32 i = inputBufferTail - 1;//Tailָ��������һ����ĸ�����λ��,-1�����һ����ĸ
				while ((inputBuffer[i] != ' ') && (i >= 1))
				{
					inputBuffer[i] = ' ';//���
					i--;
					charNumOfWord++;
					inputBufferTail--;
				}
				INT32 len = strlen(completions[0]);
				memcpy(&inputBuffer[inputBufferTail], completions[0], len);
				inputBufferTail += len;
				inputBuffer[inputBufferTail] = ' ';
				inputBufferTail++;/*��ȫ���Ժ���*/
				refreshLine(inputBuffer, inputBufferTail);
			}
			else if (lastChar == '\t')
			{
				// double tab
				int i;
				printf_s("\n");
				for (i = 0; i < num_completions; i++)
				{
					printf_s("%s", completions[i]);
					if (i % 4 == 3)
						printf_s("\n");
					else
						printf_s("\t");
				}
				//if (i % 4 != 3)
				{
					printf_s("\n\n");
				}
				//cli->showprompt = 1;
				refreshLine(inputBuffer, inputBufferTail);
			}
			continue;
		}
		else if (c == '\r')
		{
			/*�س�*/
			if (!(inputBuffer[1] == ' ' || inputBuffer[1] == '\0'))
			{
				if (cliAddHistory(cli, inputBuffer) == CLI_OK)
				{
					NumberOfHistory++;
				}
			}
			historyPos = NumberOfHistory;

			cliRunCommand(cli, &inputBuffer[1]);
			memset(inputBuffer, '\0', sizeof(inputBuffer));
			inputBuffer[0] = '>';
			inputBufferTail = 1;
			printf_s("\n>");
		}
		else if (c == '?')
		{
			/* ? ��ʾ����*/
			inputBuffer[inputBufferTail] = c;
			printf_s("%c", c);
			cliRunCommand(cli, &inputBuffer[1]);
			printf_s("\n\n");
			inputBuffer[inputBufferTail] = '\0';//ȥ��'?'
			refreshLine(inputBuffer, inputBufferTail);

		}
		else if (c == '\b')
		{
			/*�˸��*/
			GetConsoleScreenBufferInfo(handle_out, &bInfo);
			if (bInfo.dwCursorPosition.X == 1)
			{
				continue;
			}
			for (INT32 i = bInfo.dwCursorPosition.X - 1; i < inputBufferTail; i++)
			{
				inputBuffer[i] = inputBuffer[i + 1];
			}
			inputBufferTail--;
			inputBuffer[inputBufferTail] = ' ';/*�滻����ǰ���һ���ַ�*/
			refreshLine(inputBuffer, bInfo.dwCursorPosition.X - 1);
			inputBuffer[inputBufferTail] = '\0';/*�滻����*/
		}
		else if (c == (char)0xe0 || c == 0)
		{
			/*�����(��)�� 0xe04b
			�����(��)�� 0xe048
			�����(��)�� 0xe04d
			�����(��)�� 0xe050*/
			c = _getch();
			switch (c)
			{
			case 0x50:
			case 0x48:
			{
				char *his = cliHistory(cli, c, &historyPos, NumberOfHistory);
				if (his != NULL)
				{
					INT32 len = strlen(his);
					memset(&inputBuffer[1], '\0', sizeof(INPUT_BUFFER_MAX_CHAR));
					memcpy(inputBuffer, his, len + 1);
					refreshLine(inputBuffer, len);
					inputBufferTail = len;
				}

			}break;

			case 0x4d:Key_Right(inputBufferTail); break;
			case 0x4b:Key_Left(); break;
			case 83:
			{
				/*delete��*/
				GetConsoleScreenBufferInfo(handle_out, &bInfo);
				if (bInfo.dwCursorPosition.X == inputBufferTail)
				{
					continue;
				}
				for (INT32 i = bInfo.dwCursorPosition.X; i < inputBufferTail; i++)
				{
					inputBuffer[i] = inputBuffer[i + 1];
				}
				inputBufferTail--;
				inputBuffer[inputBufferTail] = ' ';/*�滻����ǰ���һ���ַ�*/
				refreshLine(inputBuffer, bInfo.dwCursorPosition.X);
				inputBuffer[inputBufferTail] = '\0';/*�滻����*/
				break;
			}
			default:
				break;
			}
		}
	}
}

/*****************************************************************
* DESCRIPTION:
*	���������ʱ�򣬹�������ƶ�һλ
* INPUTS:
*	none
* OUTPUTS:
*	none
* RETURNS:
*	none
*****************************************************************/
static void Key_Left(void)
{
	HANDLE handle_out = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO bInfo;
	COORD coursePos;

	GetConsoleScreenBufferInfo(handle_out, &bInfo);
	coursePos.X = ((bInfo.dwCursorPosition.X - 1) < 1 ? 1 : (bInfo.dwCursorPosition.X - 1));
	coursePos.Y = bInfo.dwCursorPosition.Y;
	SetConsoleCursorPosition(handle_out, coursePos);
}

/*****************************************************************
* DESCRIPTION:
*	���������ʱ�򣬹�������ƶ�һλ
* INPUTS:
*	none
* OUTPUTS:
*	none
* RETURNS:
*	none
*****************************************************************/
static void Key_Right(INT32 inputBufferTail)
{
	HANDLE handle_out = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO bInfo;
	COORD coursePos;

	GetConsoleScreenBufferInfo(handle_out, &bInfo);
	coursePos.X = (bInfo.dwCursorPosition.X + 1) > inputBufferTail ? inputBufferTail : (bInfo.dwCursorPosition.X + 1);
	coursePos.Y = bInfo.dwCursorPosition.Y;
	SetConsoleCursorPosition(handle_out, coursePos);
}


/*****************************************************************
* DESCRIPTION:
*	ˢ��һ����,�ѹ����λ��cursorpos
* INPUTS:
*	buffer - ���뻺����
*	cursorPos - �ƶ����Ĺ��λ��
* OUTPUTS:
*	none
* RETURNS:
*	none
*****************************************************************/
static void refreshLine(char *buffer, INT32 cursorPos)
{
	COORD pos, oldpos;
	CONSOLE_CURSOR_INFO cci;         //��������Ϣ�ṹ��  
	CONSOLE_SCREEN_BUFFER_INFO bInfo;
	HANDLE handle_out = GetStdHandle(STD_OUTPUT_HANDLE);

	GetConsoleScreenBufferInfo(handle_out, &bInfo);
	oldpos.X = cursorPos;
	oldpos.Y = bInfo.dwCursorPosition.Y;
	pos.X = 0;
	pos.Y = bInfo.dwCursorPosition.Y;
	cci.dwSize = CURSOR_SIZE;
	cci.bVisible = FALSE;/*�ȹرչ��,��ֹ��˸*/
	SetConsoleCursorInfo(handle_out, &cci);
	SetConsoleCursorPosition(handle_out, pos);
	for (INT32 i = 0; i < INPUT_BUFFER_MAX_CHAR;i++)
	{
		/*�������*/
		printf_s(" ");
	}
	SetConsoleCursorPosition(handle_out, pos);
	puts(buffer);
	cci.bVisible = TRUE;
	SetConsoleCursorPosition(handle_out, oldpos);
	SetConsoleCursorInfo(handle_out, &cci);
}

/*****************************************************************
* DESCRIPTION:
*	����һ���µ���
* INPUTS:
*	buf - ���뻺����
*	tail - ���뻺������β����ŵ�ָ��
* OUTPUTS:
*	none
* RETURNS:
*	none
*****************************************************************/
static void newLine(char *buf,INT32 *tail)
{
	memset(buf, '\0', INPUT_BUFFER_MAX_CHAR);
	buf[0] = '>';
	*tail = 1;
	printf_s("\n>");
}

/*****************************************************************
* DESCRIPTION:
*	��ʼ��socket
* INPUTS:
*	none
* OUTPUTS:
*	none
* RETURNS:
*	TRUE - ���óɹ�
*****************************************************************/
static INT32 socketInit(void)
{
	INT16 wVersion = MAKEWORD(2, 2);
	WSADATA wsaData;
	INT32 err = WSAStartup(wVersion, &wsaData);

	if (err != 0)
	{
		printf("\nWSAStartup failed");
		EXIT();
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("\ncould not find a usable version of Winsock.dll");
		WSACleanup();
		EXIT();
	}
	return TRUE;
}

