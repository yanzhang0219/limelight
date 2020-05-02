all:
	rm -rf ./bin
	mkdir -p ./bin
	clang -std=c99 ./src/main.c -o ./bin/limelight -O0 -g -F/System/Library/PrivateFrameworks -framework Carbon -framework SkyLight -Wall -pedantic
