.SILENT:
default:
	g++ -std=c++11 -Wall -Wextra -o "EXT3 FSCK" main.cpp
	
clean:
	rm -f "EXT3 FSCK" "EXT3 FSCK.tar.gz"

dist:
	tar -zcvf "EXT3 FSCK.tar.gz" main.cpp Makefile