CLIENT          = smtpklient
SOURCE          = isa.cpp

CC = g++
STD = -std=c++11
MAKRO = -D_GLIBCXX_USE_C99 
LIB = -static-libstdc++

TEST = test

all: $(CLIENT)

rebuild: clean all

$(CLIENT):
	$(CC) $(STD) $(MAKRO) $(LIB) $(SOURCE) -o $@

$(TEST):
	./smtpklient -i test.txt

pack:
	tar -cf xpastu00.tar isa.cpp manual.pdf Makefile README

clean:
	rm -rf *~ $(CLIENT)
