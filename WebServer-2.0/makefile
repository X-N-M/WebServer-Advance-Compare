CXX ?= g++

DEBUG ?= 0

CXXFLAGS += -std=c++17

ifeq  ($(DEBUG), 1)
	CXXFLAGS += -g
else
	CXXFLAGS += -O2

endif

server:main.cpp  ./lst_timer/lst_timer.cpp  ./log/log.cpp  ./http/http_conn.cpp  ./CGImysql/sql_connection_pool.cpp  config.cpp WebServer.cpp
	$(CXX) -o server $^ $(CXXFLAGS) -lpthread -lmysqlclient

clean:
	rm -r server