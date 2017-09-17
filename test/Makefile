all:
	g++ -g -o otest test.cpp -lpthread -ldl
	g++ -g -o test test.cpp -rdynamic ../libundead.so -lpthread -ldl
clean:
	rm test otest *deadlock.info  #*.report *.synclog
	
