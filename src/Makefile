# Compilator
CXX = g++

# Flag-uri compilare
CXXFLAGS = -Wall -Werror -pthread

# Executabil
TARGET = tema1

# Fișier sursă
SRC = main.cpp

# Directiva build
build: $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

# Directiva clean
clean:
	rm -f $(TARGET)