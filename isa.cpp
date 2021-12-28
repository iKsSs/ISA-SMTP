#include <sys/types.h>		//
#include <sys/socket.h>		//socket
#include <arpa/inet.h>		//htons

#include <netinet/in.h> /* for IP Socket data types */
#include <unistd.h> /* for close(), getopt() and fork() */
#include <netdb.h>
#include <string.h>
#include <climits>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <csignal>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <locale>         // std::locale, std::tolower

#define MAX_REPLY 1024

//////////////////////////////////
/* SOCKET */
//////////////////////////////////

class Socket{
public:
	enum ver{IP4,IP6};
protected:
	int sockfd;
	int port;
	struct sockaddr_in sin;
	struct sockaddr_in6 sin6;
	ver version;
public:
	Socket();
	void OpenSocket();
	void CloseSocket();
};

//Socket contructor with no params
Socket::Socket() {
	sockfd = 0;
	port = 0;
	sin.sin_family = AF_INET;
	sin6.sin6_family = AF_INET6;
	version = Socket::IP4;
}

//Open new socket
void Socket::OpenSocket() {
	if ( version == Socket::IP4 )
	{
		if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) { throw("Error creating socket"); }
	}
	else
	{
		if ((sockfd = socket(AF_INET6, SOCK_STREAM, 0)) < 0) { throw("Error creating socket"); }
	}
}

//Close socket
void Socket::CloseSocket() {
	if (close(sockfd) < 0) { throw("Error closing socket"); }
}

//////////////////////////////////
/* FILE */
//////////////////////////////////

class File {
protected:
	std::vector<std::string> content;
	int num_lines;
	
	virtual void parseContent(std::string) = 0;
public:
	//File contructor with no params
	File() : num_lines{ 0 } {}
	virtual void ReadFile() = 0;
	int getNumLines() { return num_lines; }
};

//////////////////////////////////
/* CLIENT */
//////////////////////////////////

class Client : public Socket, public File {
public:	
	enum e_state{
		INIT,
		HELO,
		FROM,
		TO,
		DATA,
		TEXT,
		RSET,
		SLEEP,
		QUIT,
	};

private:
	std::string name;		//ip adress OR hostname
	std::string path;
	std::string emails;
	std::string message;
	std::string err_msg;
	int seconds;			//max 3600
	char pom_str[MAX_REPLY+1];
	unsigned int size;

	e_state state;
	bool signaled;
	
	virtual void parseContent(std::string);
public:
	//Client contructor with no params
	Client() : seconds{ 0 }, size{ UINT_MAX }, state{ Client::INIT }, signaled{ false } {}
	void GetParams(int argc, char* argv[]);
	void SetPort();
	
	void DNSip();
	void Connect();
	
	virtual void ReadFile();
	void getMessage(int);
	
	bool CheckMail(std::string);

	void SendHELO();
	bool SendFROM();
	void SendTO();
	bool SendDATA();
	void SendTEXT();
	void SendRSET();
	void SendQUIT();
	
	void endWork();
	
	void Write();
	void Read();
	void ReadExtended();

	void Print();
	void PrintErr() { if ( !err_msg.empty() ) std::cerr << "ERRORS\n" << err_msg;  }
	
	void sleep();

	void setSig() { this->signaled = true; }	
	
	void printHelp();
};

//Parse message
//Params: txt - message recepient plus content
void Client::parseContent(std::string txt) {
	std::size_t found;
	
	//prepare strings
	emails.erase();
	message.erase();
	
	found = txt.find(' ');	
	
	if ( found != std::string::npos )
	{
		emails.append( txt, 0, found );
		message.append( txt.begin()+found+1, txt.end() );
	}	
	else
	{
		throw("Message not found");	
	}
}

//Parse parameters
//Params: argc - number of params
//	  argv - array of params
void Client::GetParams(int argc, char* argv[]) {
	using namespace std;

	int x;
	char c;
	
	//finite automata
	for (unsigned int i = 1; i < argc; ++i) {
		if ( argv[i][0] != '-' ) { throw("Wrong parameter"); }
		switch (argv[i][1]) {	//next char switch
			case 'a':
				if (!name.empty()) { throw("Repetition of parameter -a"); }	//not set yet
				if (argc <= ++i) { throw("Parameter not found"); }
				name = argv[i];
				if ( name.find(":") != -1 ) { version = Socket::IP6; }
				break;

			case 'p':
				if (port) { throw("Repetition of parameter -p"); }	//not set yet
				if (argc <= ++i) { throw("Parameter not found"); }
				port = atoi(argv[i]);
				break;

			case 'i':
				if (!path.empty()) { throw("Repetition of parameter -i"); }	//not set yet
				if (argc <= ++i) { throw("Parameter not found"); }
				path = argv[i];
				break;

			case 'w':
				if (seconds) { throw("Repetition of parameter -w"); }	//not set yet
				if (argc <= ++i) { throw("Parameter not found"); }
				seconds = atoi(argv[i]);
				break;

			case 'h':
				this->printHelp();
				exit(0);
				break;

			default:
				throw("Wrong parameter");
		}
	}
	
	if ( name.empty() ) { name = "127.0.0.1"; } //no address param -> default IP
	if ( !port ) { port = 25; }				  //no port param -> default port
	if ( seconds > 3600 ) { seconds = 3600; }	  //exceed 1 hour -> 1 hour
	else if ( seconds < 0 ) { seconds = 0; }

	if (path.empty())
	{
		throw("Parameter -i is required");
	}
}

//Set connection port to server
void Client::SetPort() {
	sin.sin_port = htons(port);
	sin6.sin6_port = htons(port);
}

//DNS translate - name to IP
void Client::DNSip() {
	if ( version == Socket::IP4 )
	{
 		inet_aton(name.c_str(), &(sin.sin_addr));
	}
 	else
	{
 		inet_pton(AF_INET6, name.c_str(), &(sin6.sin6_addr));
	}
}

//Connect to server
void Client::Connect() {
	if ( version == Socket::IP4 )
	{
		if (connect(sockfd, (struct sockaddr *)&sin, sizeof(sin)) < 0) { throw("Error connection"); }
	}
 	else
	{
 		if (connect(sockfd, (struct sockaddr *)&sin6, sizeof(sin6)) < 0) { throw("Error connection"); }
	}

	Read();Print();

	if ( atoi(pom_str) == 554 ) { err_msg = "Connection not estabilished\n"; raise(SIGINT); }
}

//Read file content
void Client::ReadFile() {
	std::string line;

	std::ifstream file ( path.c_str(), std::ios::in );	//open file for read
	
	if ( file.is_open() )
	{
		while ( getline (file, line) )		//parse file line by line
		{
			content.push_back(line);
			num_lines++;
		}
		file.close();
	}	
	else
	{
		throw("File unreachable");
	}	
}

//Get line from content
//Params: order - order of line (message count - current message)
void Client::getMessage(int order) {
	parseContent(content[order]);
}

//Check email address
//Params: mail - mail of recepient
bool Client::CheckMail(std::string mail) {
	std::string::size_type i=0, j=0;

	j = mail.find('@');

	if ( !( j == -1 || mail.find('.') == -1 || !std::isalnum(mail[i++]) ) )	//basic test
	{
		while ( (i<mail.length()) && (std::isalnum(mail[i]) || mail[i] == '_' || mail[i] == '-' || mail[i] == '.') ) {++i;}
		if (i > 2 && i++ == j)
		{
			while ( (i<mail.length()) && (std::isalnum(mail[i]) || mail[i] == '-') ) {++i;}
			if ((j+2) < i && i == mail.find('.', j))
			{
				j = mail.find('.', j);
				while ( (i<mail.length()) && (std::isalnum(mail[i]) || mail[i] == '-' || mail[i] == '.') ) {++i;}
				if ((j+2) < i)
				{
					return true;
				}
			}
		}
	}

	err_msg += "#Usr: " + mail + " - wrong e-mail address\n";
	return false;

	//	--g++ 4.8.4 cannot use regex.. pity, boost not allowed		
	//if( !std::regex_match (mail, std::regex("^[a-zA-Z0-9_.-]+@[a-zA-Z0-9-]+.[a-zA-Z0-9-.]+$") ) ) 
	//{
	//	err_msg += "#Usr: " + mail + " wrong e-mail address\n";
	//	continue;
	//}
}

//Send HELLO
void Client::SendHELO() {
	sprintf( pom_str, "EHLO %s \r\n", name.c_str());
	
	Write();Print();
	
	state = Client::HELO;
	
	Read();Print();

	if ( atoi(pom_str) != 250 ) 
	{ 
		err_msg = "Server reply for Extended HELLO message erroneously\n"; 
		raise(SIGINT);
	}

	if ( pom_str[3] != ' ' )		//isn't last message
	{
		ReadExtended();
	}
}

//Send email FROM
bool Client::SendFROM() {
	sprintf( pom_str, "MAIL FROM:<xpastu00@isa.local> \r\n");
	
	Write();Print();
	
	state = Client::FROM;
	
	Read();Print();

	if ( atoi(pom_str) != 250 ) 
	{ 
		err_msg += "Server reply for MAIL FROM message erroneously\n"; 
		SendRSET();
		return false;
	}
	else
	{
		return true;
	}
}

//Send email TO
void Client::SendTO() {
	std::size_t found;
	std::string mail, mails = emails;
	int end = 0;
	
	state = Client::TO;

	do
	{
		found = mails.find(',');
			
		if ( found != std::string::npos )
		{
			mail = mails.substr( 0, found );
			mails.erase( 0, found+1 );
			
			if ( !CheckMail(mail) ) { continue; }

			sprintf( pom_str, "RCPT TO:<%s> \r\n", mail.c_str());
			
			Write();Print();
			
			Read();Print();
		}
		else
		{
			end = 1;
			mail = mails;

			if ( !CheckMail(mail) ) { continue; }
			
			sprintf( pom_str, "RCPT TO:<%s> \r\n", mail.c_str());
			
			Write();Print();
					
			Read();Print();
		}
		
		if ( atoi(pom_str) != 250 ) 
		{
			err_msg += "#Usr: " + mail + " not found - could't deliver e-mail\n";
		}

	} while ( end != 1 );
}

//Send DATA
bool Client::SendDATA() {
	if ( message.length() > this->size )
	{
		err_msg += "#Msg: \"" + message.substr(0,20) + "...\" "; 
		err_msg += "could't be sent - length too long (MAX " + std::to_string(this->size) + ")\n";
		return false;
	}

	for (char& c : message) {	// Check chars in msg if printable - before send DATA
		if (!isprint(c))
		{
			err_msg += "#Msg: \"" + message.substr(0,25); 
			if ( message.length() > 25 ) { err_msg += "..."; }
			err_msg += "\" contain not allowed characters\n";
			return false;
		}
	}

	sprintf( pom_str, "DATA \r\n");
	
	Write();Print();
	
	state = Client::DATA;
	
	Read();Print();

	if ( atoi(pom_str) == 554 ) 
	{
		err_msg += "#Msg: \"" + message.substr(0,25); 
		if ( message.length() > 25 ) { err_msg += "..."; }
		err_msg += "\" could't be sent - no valid recipient found\n";
		return false;
	}
	else if ( atoi(pom_str) == 354 ) //everything OK
	{
		return true;
	}
	else	//another error not log
	{ 
		return false; 
	}
}

//Send email content
void Client::SendTEXT() {
	sprintf( pom_str, "%s\r\n.\r\n", message.c_str());
	
	Write();Print();
	
	state = Client::TEXT;
	
	Read();Print();

	if ( atoi(pom_str) != 250 ) 
	{ 
		err_msg += "Server reply for TEXT erroneously\n"; 
		SendRSET();
	}
}

//Send reset to actual email message
void Client::SendRSET() {
	sprintf( pom_str, "RSET \r\n");
	
	Write();Print();
	
	state = Client::RSET;
	
	Read();Print();
}

//Send QUIT
void Client::SendQUIT() {
	sprintf( pom_str, "QUIT \r\n");
	
	Write();Print();
	
	state = Client::QUIT;

	Read();Print();
}

//End Client - if DATA send, complete email content send if SIG occured
void Client::endWork()
{
	if ( signaled )		//SIG interrupted
	{
		if ( state == Client::DATA )	//DATA then finish sending
		{
			SendTEXT();
		}
		else 				//RSET sending
		{
			SendRSET();
		}
	}
	else			//normal run
	{
		sleep();
	}

	PrintErr();
	
	if ( state > Client::HELO )	//bounded connection with server
	{
		SendQUIT();	
	}

	if ( state > Client::INIT )
	{
		CloseSocket();
	}
}

//Write to server
void Client::Write() {
	if ( write( sockfd, pom_str, strlen( pom_str ) ) < 0) { throw("Error sending message"); }
}

//Read from server
void Client::Read() {
	int i = 0, count = 0;
	char c;

	bzero(pom_str, sizeof(pom_str));	//Clear input buffer
	
	while( ++count < MAX_REPLY && read( sockfd, &c, 1 ) > 0 && c != '\n' )
	{
		pom_str[i++] = c;
	}
	
	if ( c == '\n' )
	{
		pom_str[i++] = c;
	}
	
	pom_str[i] = '\0';		//close char*
}

//Read extended information from server
void Client::ReadExtended() {
	std::string s;
	int i;

	do{
		Read();Print();
		
		if ( atoi(pom_str) != 250 ) { err_msg = "Connection extension errorr\n"; raise(SIGINT); }

		s = pom_str;
		s = s.substr(4);
		i = s.find(' '); 

		if ( s.substr(0, i) == "SIZE") { this->size = (std::stoi(s.substr(i+1)) == 0 ? this->size : std::stoi(s.substr(i+1))); } 
		
	} while ( pom_str[3] == '-' ); 
}

//Keep connection alive
void Client::sleep() { 
	int x = seconds / 300;
	int z = x;
	
	state = Client::SLEEP;

	sprintf( pom_str, "NOOP \r\n");

	while ( x-- > 0 )		//every 5 minutes send NOOP
	{
		usleep(300*1000000);
	
		Write();Print();

		Read();Print();
	}

	seconds -= (300 * z);

	usleep(seconds*1000000); 	
}

//Print data from the server - auxiliary function
void Client::Print() {
	//puts(this->pom_str);
}

//Print help
void Client::printHelp() {
	std::string str = "\
Popis:\n\
\tKonzolova aplikace implementujici jednoducheho SMTP klienta, ktery je schopny pripojit se na SMTP server a nasledne odeslat postu.\n\
Autor:\n\
\tJakub Pastuszek - xpastu00\n\n\
./smtpklient [ -a IP ] [ -p port ] [ -i subor ] [ -w sekund ] [ -h ]\n\n\
\tIP - (nepovinny, vychodzia hodnota je 127.0.0.1) IP adresa SMTP servera (pocitajte s moznostou zadat IPv4 aj IPv6 adresu)\n\
\tport - (nepovinny, vychodzia hodnota je 25) port, na ktorom SMTP server ocakava prichadzajuce spojenia\n\
\tsubor - (povinny parameter) cesta k suboru, v ktorom sa nachadzaju spravy pre odoslanie\n\
\tsekund - (nepovinny parameter, vychodzia hodnota 0) po odoslani poslednej spravy sa neukonci spojenie okamzite, \
ale klient bude umelo udrzovat spojenie otvorene po dobu specifikovanu tymto parametrom. Najvyssia hodnota, ktoru je mozne zadat je jedna hodina.\
Priklad pouziti - soubor \"mail.txt\" se seznamem prijemcu a zprav (viz manual.pdf) pro odeslani:\n\
./smtpklient -i mail.txt\n\
\n";

	std::cout << str << std::endl;
}

//////////////////////////////////
/* INSTANCE */
//////////////////////////////////

Client c;

//////////////////////////////////
/* FUNCTIONS */
//////////////////////////////////

//SIG handler
void handler(int x)	
{
	//std::cerr << "Application is closing.." << std::endl; // after pressing SIG - INT, TERM, QUIT you'll see this message

	c.setSig();
	c.endWork();

	exit(0);
}

//Client side
void StartClient(int argc, char* argv[]) {
	int max;

	c.GetParams(argc, argv);
	c.ReadFile();

	c.OpenSocket();
	c.SetPort();
	c.DNSip();
	c.Connect();
	
	c.SendHELO();	
	
	max = c.getNumLines();
	
	for ( int i = 0; i < max; ++i )
	{
		c.getMessage(i);
		
		if ( !c.SendFROM() ) { continue; }	

		c.SendTO();	

		if ( c.SendDATA() ) 
		{
			c.SendTEXT();
		}
		else
		{
			c.SendRSET();
		}
	} 
	
	c.endWork();
}

//////////////////////////////////
/* MAIN */
//////////////////////////////////

int main(int argc, char* argv[]) {
	try {
		signal(SIGINT, handler);
		signal(SIGTERM, handler);
		signal(SIGQUIT, handler);
		StartClient(argc, argv);
	}
	catch (const char* e) { //print my err msg to STDERR
		std::cerr << e << std::endl;
		exit(-1);
	}
	catch (...) { //print just ERROR to STDERR if unknown exception
		std::cerr << "Unspecified ERROR" << std::endl;
		exit(-2);
	}
}
