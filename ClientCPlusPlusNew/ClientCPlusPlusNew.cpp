// ClientCPlusPlusNew.cpp : Этот файл содержит функцию "main". Здесь начинается и заканчивается выполнение программы.
//

#include <iostream>
#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <fstream>
#include <thread>
#include <chrono>
#include <regex>
#include <cstring>
#include <sstream>
#pragma comment (lib, "Ws2_32.lib")

#define BUFFLEN 1024

int main(int argc, char* argv[])
{
	WSAData wsaData;
	WORD DLLVersion = MAKEWORD(2, 1); //дает версию 
	if (WSAStartup(DLLVersion, &wsaData) != 0) //Загружаем библиотеку
	{
		std::cout << "Error : Library did not load" << std::endl;
		exit(1);
	}

	SOCKADDR_IN adrSock; //хранение адреса сокета
	static const char LOCAL_HOST[] = "127.0.0.1";
	int sizeAddr = sizeof(adrSock);
	inet_pton(AF_INET, LOCAL_HOST, &(adrSock.sin_addr)); // функция преобразует строку символов src в структуру сетевого адреса 
														 // сетевого семейства адресов af, а затем копирует полученную структуру 
														 // по адресу dst. 
	adrSock.sin_port = htons(8080); // sin_port содержит номер порта, который намерен занять процесс.
	adrSock.sin_family = AF_INET;

	SOCKET Connection = socket(AF_INET, SOCK_STREAM, NULL); // Для создания сокета используется системный вызов socket.
															// s = socket(domain, type, protocol);
	if (connect(Connection, (SOCKADDR*)&adrSock, sizeof(adrSock)) != 0)
	{
		std::cout << "Error : Connection was failed";
		return 1;
	}

	const char* sendbuf = "GET / HTTP/1.1\r\n\r\n";
	send(Connection, sendbuf, strlen(sendbuf), NULL); // Функция служит для записи данных в сокет. 
																			   //Первый аргумент - сокет - дескриптор, в который записываются данные.
																			   //Второй и третий аргументы - соответственно, адрес и длина буфера с записываемыми данными.
																			   //Четвертый параметр - это комбинация битовых флагов, управляющих режимами записи.
																			   //Если аргумент flags равен нулю, то запись в сокет(и, соответственно, считывание) происходит в порядке поступления байтов.
																			   //Если значение flags есть MSG_OOB, то записываемые данные передаются потребителю вне очереди.

	char wmsg[BUFFLEN];
	size_t n = recv(Connection, wmsg, BUFFLEN - 1, NULL); // Функция служит для чтения данных из сокета. 
												// Первый аргумент - сокет - дескриптор, из которого читаются данные.
												// Второй и третий аргументы - адрес и длина буфера для записи читаемых данных.
												// Четвертый параметр - это комбинация битовых флагов, управляющих режимами чтения.
												// Если аргумент flags равен нулю, то считанные данные удаляются из сокета.
												// Если значение flags MSG_PEEK, то данные не удаляются и могут быть считаны последущим вызовом(или вызовами) recv.
	if (n == 0 || n == -1)
	{
		std::cerr << "recv error " << n << std::endl;
		exit(1);
	}
	wmsg[n] = 0;
	std::cout << wmsg;

	// ===============================================================================
	bool closeConnection = false;
	std::string fileReq;
	std::ofstream file;

	do
	{
		Connection = socket(AF_INET, SOCK_STREAM, NULL); // Для создания сокета используется системный вызов socket.
															// s = socket(domain, type, protocol);
		if (connect(Connection, (SOCKADDR*)&adrSock, sizeof(adrSock)) != 0)
		{
			std::cerr << "Error : Connection was failed";
			return 1;
		}

		std::cout << "File name: ";
		//std::cin.get();
		std::getline(std::cin, fileReq);
		std::cout << "Actions: 1-ReadFile, 2-WriteFile, 3-DeleteFile" << std::endl;
		int action;
		std::cin >> action;
		switch (action)
		{
		case 1: {
			std::string req = "GET /" + fileReq + " HTTP/1.1\r\n\r\n";

			int sendInf = send(Connection, req.c_str(), req.size(), NULL);
			if (sendInf == 0 || sendInf == -1)
			{
				closeConnection = true;
				break;
			}
			n = recv(Connection, wmsg, BUFFLEN - 1, NULL);
			if (n == 0 || n == -1)
			{
				std::cerr << "recv error " << n << std::endl;
				closeConnection = true;
				break;
			}
			wmsg[n] = 0;
			std::cout << wmsg << std::endl;
			break;
		}
		case 2: {
			std::string req = "POST /" + fileReq + " HTTP/1.1\r\n\r\n";
			std::string text;
			std::cin >> text;
			req += "Content-Type: text/plain\r\nHost: localhost:8080\r\nContent-Length: ";
			req += text.length();
			req += "\r\n";
			req += text;
			int sendInf = send(Connection, req.c_str(), req.size(), NULL);
			if (sendInf == 0 || sendInf == -1)
			{
				closeConnection = true;
				break;
			}
			n = recv(Connection, wmsg, BUFFLEN - 1, NULL);
			if (n == 0 || n == -1)
			{
				std::cerr << "recv error " << n << std::endl;
				closeConnection = true;
				break;
			}
			break;
		}
		case 3: {
			std::string req = "DELETE /" + fileReq + " HTTP/1.1\r\n\r\n";

			int sendInf = send(Connection, req.c_str(), req.size(), NULL);
			if (sendInf == 0 || sendInf == -1)
			{
				closeConnection = true;
				break;
			}
			n = recv(Connection, wmsg, BUFFLEN - 1, NULL);
			if (n == 0 || n == -1)
			{
				std::cerr << "recv error " << n << std::endl;
				closeConnection = true;
				break;
			}
			break;
		}
		default:
			std::cerr << "No such action" << std::endl;
			break;
		}


		//	do
		//	{
		//		memset(bufferFile, 0, BUFFER_SIZE); // memset() используется для присвоения начальных значений определенной области памяти.
		//		byRecv = recv(Connection, bufferFile, BUFFER_SIZE, NULL);
		//		if (byRecv == 0 || byRecv == -1)
		//		{
		//			closeConnection = true;
		//			break;
		//		}
		//		file.write(bufferFile, byRecv);
		//		fileDownloaded += byRecv;
		//	} while (fileDownloaded < fileRequestedsize);
		//	file.close();
		//}
		//else if (codeFile == 404)
		//{
		//	std::cout << "File was not found" << std::endl;
		//}
	} while (!closeConnection);

	system("pause");
	return 0;

}