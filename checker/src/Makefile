# Makefile

# Definirea compilatorului C++
CXX = g++

# Flag-uri de compilare:
# -Wall    : Activează toate avertismentele
# -Werror  : Tratatește avertismentele ca erori
# -pthread : Include suport pentru Pthreads
CXXFLAGS = -Wall -Werror -pthread

# Numele executabilului
TARGET = tema1

# Lista fișierelor sursă
SRC = main.cpp

# Target-ul implicit (all)
all: $(TARGET)

# Regula pentru compilarea executabilului
$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

# Target pentru build
build: $(TARGET)

# Target pentru curățare
clean:
	rm -f $(TARGET)
