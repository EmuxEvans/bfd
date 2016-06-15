#include "stdio.h"
#include "windows.h"
#include "conio.h"
#include "console.h"
#include "libcli.h"
#include "bfd_cli.h"
#include "bfd.h"
#include "common.h"

INT32 test(struct cli_def *cli, const char *command, char *argv[], int argc)
{
	printf_s("\narg:\n");
	for (INT32 i = 0; i < argc; i++)
	{
		printf_s("%s ", argv[i]);
	}
	return 0;
}

int main(void)
{
	char inputBuffer[INPUT_BUFFER_MAX_CHAR];
	CONSOLE_CURSOR_INFO cci;         //定义光标信息结构体  
	CONSOLE_SCREEN_BUFFER_INFO bInfo;
	HANDLE handle_out = GetStdHandle(STD_OUTPUT_HANDLE);
	cci.bVisible = TRUE;
	cci.dwSize = CURSOR_SIZE;
	SetConsoleCursorInfo(handle_out, &cci);
	SetConsoleTitle(L"BFD测试");

	socketInit();

	struct cli_def *cli = cliInit();
	struct cli_command *com = NULL;
	char argHelp[INPUT_BUFFER_MAX_CHAR];
	memset(argHelp, '\0', sizeof(argHelp));

	com = cliRegisterCommand(cli, NULL, "bfd", NULL, "BFD configuration commands", CLI_ARGUMENT_TYPE_NONE, NULL);
	com = cliRegisterCommand(cli, com, "session", cliBfdCreateSession, "BFD Session configuration commands", CLI_ARGUMENT_TYPE_IP, \
							"<A.B.C.D>	Destination IP Address of BFD Session");
	sprintf_s(argHelp, "<%d-%d>		Milliseconds", MIN_RX_INTERVAL/1000, MAX_RX_INTERVAL/1000);
	com = cliRegisterCommand(cli, com, "min-receive-interval", cliBfdSetRxInterval, "    Minimum receive interval capability", CLI_ARGUMENT_TYPE_NUMBER, \
								argHelp);
	sprintf_s(argHelp, "<%d-%d>		Milliseconds", MIN_TX_INTERVAL / 1000, MAX_TX_INTERVAL / 1000);
	com = cliRegisterCommand(cli, com, "min-transmit-interval", cliBfdSetTxInterval, "    Transmit interval between BFD packets", CLI_ARGUMENT_TYPE_NUMBER, \
								argHelp);
	sprintf_s(argHelp, "<%d-%d>		value used to multiply the interval", MIN_MULT, MAX_MULT);
	cliRegisterCommand(cli, com, "multiplier", cliBfdSetMultiplier, " Multiplier value used to compute holddown", CLI_ARGUMENT_TYPE_NUMBER,\
								argHelp);
	cliBuildShortest(cli, cli->commands);

	/*2016年5月26日 01:23:15*/
	/*应该让每个命令自己创建 补全的函数*/
	memset(inputBuffer, '\0', sizeof(inputBuffer));
	inputBuffer[0] = '>';
	printf_s(">");

	INT32 inputBufferTail = 1;
	INT32 historyPos = 0;
	INT32 NumberOfHistory = 0;
	char c = 0;
	char lastChar = 0;

	/*测试用*/
	char test[] = "bfd s 192.168.80.100 m 500 m 500 m 40";
	cliRunCommand(cli, test);

	while (1)
	{
		lastChar = c;
		c = _getch();
		if ((c >= '0'&&c <= '9') || (c >= 'a'&&c <= 'z') || (c >= 'A'&&c <= 'Z') || (c == ' ')\
			|| (c == '/') || (c == '\\') || (c == '.'))
		{
			/*字符*/
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
				INT32 i = inputBufferTail - 1;//Tail指向的是最后一个字母后面的位置,-1是最后一个字母
				while ((inputBuffer[i] != ' ') && (i >= 1))
				{
					inputBuffer[i] = ' ';//清空
					i--;
					charNumOfWord++;
					inputBufferTail--;
				}
				INT32 len = strlen(completions[0]);
				memcpy(&inputBuffer[inputBufferTail], completions[0], len);
				inputBufferTail += len;
				inputBuffer[inputBufferTail] = ' ';
				inputBufferTail++;/*补全了以后在*/
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
			/*回车*/
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
			inputBuffer[inputBufferTail] = c;
			printf_s("%c", c);
			cliRunCommand(cli, &inputBuffer[1]);
			printf_s("\n\n");
			inputBuffer[inputBufferTail] = '\0';//去掉'?'
			refreshLine(inputBuffer, inputBufferTail);

		}
		else if (c == '\b')
		{
			/*退格键*/
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
			inputBuffer[inputBufferTail] = ' ';/*替换掉以前最后一个字符*/
			refreshLine(inputBuffer, bInfo.dwCursorPosition.X - 1);
			inputBuffer[inputBufferTail] = '\0';/*替换回来*/
		}
		else if (c == (char)0xe0 || c == 0)
		{
			/*方向键(←)： 0xe04b
			方向键(↑)： 0xe048
			方向键(→)： 0xe04d
			方向键(↓)： 0xe050*/
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
				/*delete键*/
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
				inputBuffer[inputBufferTail] = ' ';/*替换掉以前最后一个字符*/
				refreshLine(inputBuffer, bInfo.dwCursorPosition.X);
				inputBuffer[inputBufferTail] = '\0';/*替换回来*/
				break;
			}
			default:
				break;
			}
		}
	}
}
