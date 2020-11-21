#include "MapleNet.hpp"


UDPClient::UDPClient()
{
	isStarted = false;
	isLoopStarted = false;
	write_out = false;
	memset((void*)to_send, 0, 256);
	last_sent = "";
	disconnect_toggle = false;
};

void UDPClient::SetHost(std::string host, int port)
{
	host_addr.sin_family = AF_INET;
	host_addr.sin_port = htons((u16)port);
	inet_pton(AF_INET, host.data(), &host_addr.sin_addr);
}

int UDPClient::SendData(std::string data)
{
	// sets current send buffer
	write_out = true;
	memcpy((void*)to_send, data.data(), maplenet.PayloadSize());
	write_out = false;

	if (maplenet.client_input_authority)
		maplenet.AddNetFrame((const char*)to_send);

	return 0;
}

// http://www.concentric.net/~Ttwang/tech/inthash.htm
unsigned long mix(unsigned long a, unsigned long b, unsigned long c)
{
    a=a-b;  a=a-c;  a=a^(c >> 13);
    b=b-c;  b=b-a;  b=b^(a << 8);
    c=c-a;  c=c-b;  c=c^(b >> 13);
    a=a-b;  a=a-c;  a=a^(c >> 12);
    b=b-c;  b=b-a;  b=b^(a << 16);
    c=c-a;  c=c-b;  c=c^(b >> 5);
    a=a-b;  a=a-c;  a=a^(c >> 3);
    b=b-c;  b=b-a;  b=b^(a << 10);
    c=c-a;  c=c-b;  c=c^(b >> 15);
    return c;
}

// udp ping, seeds with random number
int UDPClient::PingOpponent(int add_to_seed)
{
	unsigned long seed = mix(clock(), time(NULL), getpid());
	srand(seed + add_to_seed);
	//srand(time(NULL));
	int rnd_num_cmp = rand() * 1000 + 1;

	if (ping_send_ts.count(rnd_num_cmp) == 0)
	{

		std::stringstream ping_ss("");
		ping_ss << "PING " << rnd_num_cmp;
		std::string to_send_ping = ping_ss.str();
		
		sendto(local_socket, (const char*)to_send_ping.data(), strlen(to_send_ping.data()), 0, (const struct sockaddr*)&opponent_addr, sizeof(opponent_addr));
		INFO_LOG(NETWORK, "Sent %s", to_send_ping.data());

		long current_timestamp = unix_timestamp();
		ping_send_ts.emplace(rnd_num_cmp, current_timestamp);
	}
	
	// last ping key
	return rnd_num_cmp;
}

int UDPClient::GetOpponentAvgPing()
{
	for (int i = 0; i < 5; i++)
	{
		PingOpponent(i);
	}

	return avg_ping_ms;
}

void UDPClient::StartSession()
{
	maplenet.session_started = true;
	std::string to_send_start("START");

	for (int i = 0; i < settings.maplenet.PacketsPerFrame; i++)
	{
		sendto(local_socket, (const char*)to_send_start.data(), strlen(to_send_start.data()), 0, (const struct sockaddr*)&opponent_addr, sizeof(opponent_addr));
	}

	INFO_LOG(NETWORK, "Session Initiated");
}

void UDPClient::EndSession()
{
	disconnect_toggle = true;
	std::string to_send_end("DISCONNECT");

	for (int i = 0; i < settings.maplenet.PacketsPerFrame; i++)
	{
		sendto(local_socket, (const char*)to_send_end.data(), strlen(to_send_end.data()), 0, (const struct sockaddr*)&opponent_addr, sizeof(opponent_addr));
	}

	INFO_LOG(NETWORK, "Disconnected");
}

sock_t UDPClient::createAndBind(int port)
{
	sock_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (!VALID(sock))
	{
		ERROR_LOG(NETWORK, "Cannot create server socket");
		return sock;
	}
	int option = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&option, sizeof(option));

	struct sockaddr_in serveraddr;
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons((u16)port);

	if (::bind(sock, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) < 0)
	{
		ERROR_LOG(NETWORK, "MapleNet UDP Server: bind() failed. errno=%d", get_last_error());
		closeSocket(sock);
	}
	else
		set_non_blocking(sock);

	return sock;
}

bool UDPClient::createLocalSocket(int port)
{
	if (!VALID(local_socket))
		local_socket = createAndBind(port);

	return VALID(local_socket);
}

bool UDPClient::Init(bool hosting)
{
	if (!settings.maplenet.Enable)
		return false;
#ifdef _WIN32
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0)
	{
		ERROR_LOG(NETWORK, "WSAStartup failed. errno=%d", get_last_error());
		return false;
	}
#endif

	if (hosting)
	{
		return createLocalSocket(stoi(settings.maplenet.ServerPort));
	}
	else
	{
		opponent_addr = host_addr;
		return createLocalSocket(0);
	}
		
}

long UDPClient::unix_timestamp()
{
    time_t t = time(0);
    long int now = static_cast<long int> (t);
    return now;
}

void UDPClient::ClientLoop()
{
	isLoopStarted = true;

	while (true)
	{
		while (true)
		{
			if (disconnect_toggle)
			{
				EndSession();
				gui_open_disconnected();
			}

			// if match has not started, send packet to inform host who the opponent is
			if (maplenet.FrameNumber > 0 && !maplenet.hosting)
			{
				if (!maplenet.isMatchStarted)
				{
					sendto(local_socket, (const char*)to_send, maplenet.PayloadSize(), 0, (const struct sockaddr*)&host_addr, sizeof(host_addr));
				}
			}

			// if opponent detected, shoot packets at them
			if (opponent_addr.sin_port > 0 &&
				memcmp(to_send, last_sent.data(), maplenet.PayloadSize()) != 0)
			{
				// send payload until morale improves
				for (int i = 0; i < settings.maplenet.PacketsPerFrame; i++)
				{
					sendto(local_socket, (const char*)to_send, maplenet.PayloadSize(), 0, (const struct sockaddr*)&opponent_addr, sizeof(opponent_addr));
				}

				if (settings.maplenet.Debug == DEBUG_SEND ||
					settings.maplenet.Debug == DEBUG_SEND_RECV ||
					settings.maplenet.Debug == DEBUG_ALL)
				{
					maplenet.PrintFrameData("Sent", (u8 *)to_send);
				}

				last_sent = std::string(to_send, to_send + maplenet.PayloadSize());

				if (maplenet.client_input_authority)
					maplenet.AddNetFrame((const char*)to_send);
			}

			struct sockaddr_in sender;
			socklen_t senderlen = sizeof(sender);
			char buffer[256];
			memset(buffer, 0, 256);
			int bytes_read = recvfrom(local_socket, buffer, sizeof(buffer), 0, (struct sockaddr*)&sender, &senderlen);
			if (bytes_read)
			{
				if (memcmp("PING", buffer, 4) == 0)
				{
					char buffer_copy[256];
					memcpy(buffer_copy, buffer, 256);
					buffer_copy[1] = 'O';
					sendto(local_socket, (const char*)buffer_copy, strlen(buffer_copy), 0, (struct sockaddr*)&sender, senderlen);
					memset(buffer, 0, 256);
				}

				if (memcmp("PONG", buffer, 4) == 0)
				{
					int rnd_num_cmp = atoi(buffer + 5);
					long ret_timestamp = unix_timestamp();

					
					if (ping_send_ts.count(rnd_num_cmp) == 1)
					{

						long rtt = ret_timestamp - ping_send_ts[rnd_num_cmp];
						INFO_LOG(NETWORK, "Received PONG %d, RTT: %d ms", rnd_num_cmp, rtt);
						
						ping_rtt.push_back(rtt);

						if (ping_rtt.size() > 1)
						{
							avg_ping_ms = std::accumulate(ping_rtt.begin(), ping_rtt.end(), 0.0) / ping_rtt.size();
						}
						else
						{

							avg_ping_ms = rtt;
						}

						if (ping_rtt.size() > 5)
							ping_rtt.clear();

						ping_send_ts.erase(rnd_num_cmp);
					}
					
				}

				if (memcmp("START", buffer, 5) == 0)
				{
					maplenet.session_started = true;
				}

				if (memcmp("DISCONNECT", buffer, 10) == 0)
				{
					disconnect_toggle = true;
				}

				if (bytes_read == maplenet.PayloadSize())
				{
					if (!maplenet.isMatchReady && maplenet.GetPlayer((u8 *)buffer) == maplenet.opponent)
					{
						opponent_addr = sender;

						// prepare for delay selection
						if (maplenet.hosting)
							maplenet.OpponentIP = std::string(inet_ntoa(opponent_addr.sin_addr));

						maplenet.isMatchReady = true;
						maplenet.resume();
					}

					maplenet.ClientReceiveAction((const char*)buffer);
				}
			}

			//maplenet.ClientLoopAction();
			//wait_seconds(0.006f);
		}
	}
}

void UDPClient::ClientThread()
{
	Init(maplenet.hosting);
	ClientLoop();
}
