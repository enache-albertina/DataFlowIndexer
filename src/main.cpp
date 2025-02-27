#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <atomic>
#include <memory>
#include <functional>
#include <future>
#include <queue>
#include <locale>


const int ALPHABET_SIZE = 26;

// structura ce retine argumentele din input
struct InputArgs {
    int num_mappers; // numarul de thread-uri mapper
    int num_reducers; // numarul de thread-uri reducer
    std::string input_file; // numele fisierului de input
};

// Functie pentru parsarea argumentelor din input
InputArgs parseInputArgs(int argc, char** argv) {
    if (argc != 4) {
        throw std::invalid_argument("Numar invalid de argumente");
    }


    InputArgs args;
    args.num_mappers = std::stoi(argv[1]);
    args.num_reducers = std::stoi(argv[2]);
    args.input_file = argv[3];
    // verifica daca numerele sunt pozitive
    if (args.num_mappers <= 0 || args.num_reducers <= 0) {
        throw std::invalid_argument("Numarul de mappers si reducers trebuie sa fie pozitiv");
    }
    return args;
}

// Clasă pentru gestionarea cozii de fișiere cu thread-safety
class ThreadSafeFilesQueue {
private:
    std::vector<std::string> files_name; // vector cu numele fisierelor
    std::atomic<size_t> current_index{0}; // indexul curent
    mutable std::mutex mutex; // Declarați mutex-ul ca mutable

public:
    // metoda pentru adaugarea unui nou fisier in coada
    void addFile(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mutex);
        files_name.push_back(filename);
    }
    // obtin urmatorul fisier din coada
    // returneaza false daca nu mai sunt fisiere disponibile
    bool getNextFile(std::string& file_name, int& file_id) {
        std::lock_guard<std::mutex> lock(mutex);
        if(current_index < files_name.size()) {
            file_id = current_index + 1; // ID începe de la 1
            file_name = files_name[current_index];
            current_index++;
            return true;
        }
        return false;
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex);
        return files_name.size();
    }
};

// Pool de thread-uri pentru procesare eficientă
class ThreadPool {
private:
    std::vector<std::thread> workers; // vector de thread-uri worker
    std::queue<std::function<void()>> tasks; // coada de task-uri
    std::mutex queue_mutex; // mutex pentru coada de task-uri
    std::condition_variable condition; // variabila de conditie pentru a sincroniza
    std::atomic<bool> stop{false}; // flag pentru oprirea pool-ului

public:
    // constructoe - creeaza si porneste thread-urile worker
    ThreadPool(size_t threads) {
        for(size_t i = 0; i < threads; ++i)
            workers.emplace_back([this] {
                for(;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        // asteapta pana cand exista un task sau pool-ul este oprit
                        this->condition.wait(lock, [this]{ 
                            return this->stop || !this->tasks.empty(); 
                        });
                        if(this->stop && this->tasks.empty())
                            return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
    }
    // adauga un task in pool si returneaza un future pentru rezultat
    template<class F>
    auto enqueue(F&& f) -> std::future<typename std::result_of<F()>::type> {
        using return_type = typename std::result_of<F()>::type;
        
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::forward<F>(f)
        );
        
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if(stop)
                throw std::runtime_error("Încearcă să adaugi task după oprirea thread pool-ului");
            
            tasks.emplace([task](){ (*task)(); });
        }
        condition.notify_one();
        return res;
    }
    // destructor - opreste toate thread-urile si asteapta finalizarea lor
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for(std::thread &worker: workers)
            worker.join();
    }
};

// Clasa pentru controlul procesului de mapping
class MappingControl {
private:
    std::mutex mutex_;
    std::condition_variable cond_;
    std::atomic<bool> mapping_done_{false};

public:
    void setDone() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            mapping_done_ = true;
        }
        cond_.notify_all();
    }

    void waitForDone() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return mapping_done_.load(); }); // Folosiți .load()
    }
};

// Clasa pentru gestionarea datelor reducerilor
class ReducerData {
private:
    std::map<std::string, std::set<int>> word_map;
    std::mutex mutex;

public:
    void addWord(const std::string& word, int file_id) {
        std::lock_guard<std::mutex> lock(mutex);
        word_map[word].insert(file_id);
    }

    std::vector<std::pair<std::string, std::set<int>>> getWordsForLetter(char letter) {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<std::pair<std::string, std::set<int>>> result;
        
        for(const auto& entry : word_map) {
            if(entry.first[0] == letter) {
                result.push_back(entry);
            }
        }
        return result;
    }
};

// Functie pentru normalizarea cuvintelor cu suport internațional
std::string normalizeWord(const std::string& word) {
    std::string normalized;
    std::locale loc;
    for(char c : word) {
        if(std::isalpha(c, loc)) {
            normalized += std::tolower(c, loc);
        }
    }
    return normalized;
}

// Comparator pentru sortarea cuvintelor 
bool compareWords(const std::pair<std::string, std::set<int>>& a, 
                  const std::pair<std::string, std::set<int>>& b) {
    if(a.second.size() != b.second.size()) {
        return a.second.size() > b.second.size(); // Descrescator după numarul de fisiere
    }
    return a.first < b.first; // Alfabetic
}



// Functie pentru gestionarea erorilor de thread
void handleThreadError(const std::string& errorMessage) {
    std::cerr << "Eroare la thread: " << errorMessage << std::endl;
    std::terminate();
}


// Functia Mapper
void mapperFunction(ThreadSafeFilesQueue& queue, 
                    std::vector<std::unique_ptr<ReducerData>>& reducers, 
                    int num_reducers) {
    std::string file_name;
    int file_id;
    // preiau din coada de fisiere si atribui cate un reducer
    while(queue.getNextFile(file_name, file_id)) {
        try {
            
            std::ifstream file(file_name);
            if(!file.is_open()) {
                std::cerr << "Eroare la deschiderea fișierului: " << file_name << std::endl;
                continue;
            }

            // determina cuvintele unice din fisier
            std::set<std::string> unique_words;
            std::string word;
            while(file >> word) {
                std::string normalized = normalizeWord(word);
                if(normalized.empty()) continue;
                unique_words.insert(normalized);
            }
            file.close();
           
          

            // Distribuie cuvintele catre Reduceri
            for(const auto& w : unique_words) {
                if(w.empty()) continue;

                char first_letter = w[0];
                if(first_letter < 'a' || first_letter > 'z') continue;

                // Determină Reducer-ul responsabil pentru această literă
                int letter_pos = first_letter - 'a';
                int letters_per_reducer = ALPHABET_SIZE / num_reducers;
                int extra_letters = ALPHABET_SIZE % num_reducers;
                
                int reducer_index = 0;
                int cumulative_letters = 0;
                for(int i = 0; i < num_reducers; i++) {
                    int current_reducer_letters = letters_per_reducer + (i < extra_letters ? 1 : 0);
                    if(letter_pos < cumulative_letters + current_reducer_letters) {
                        reducer_index = i;
                        break;
                    }
                    cumulative_letters += current_reducer_letters;
                }

                // Adauga cuvantul la Reducer-ul corespunzator
                reducers[reducer_index]->addWord(w, file_id);
            }
            
        }
        catch(const std::exception& e) {
            handleThreadError("Eroare în mapper: " + std::string(e.what()));
        }
      
       
    }
}

// Funcția Reducer
void reducerFunction(std::vector<char> letters, 
                     ReducerData* data, 
                     MappingControl& control) {
    // Așteapta finalizarea mapping-ului
    control.waitForDone();

    // Pentru fiecare litera atribuita acestui Reducer
    for(auto letter : letters) {
        // Colecteaza cuvintele care încep cu aceasta litera
        auto words = data->getWordsForLetter(letter);

        if(words.empty()) continue;

        // Sortează cuvintele conform cerințelor
        std::sort(words.begin(), words.end(), compareWords);

        // Creeaza fisierul de ieșire pentru aceasta litera
        std::string output_filename = std::string(1, letter) + ".txt";
        std::ofstream output(output_filename);
        
        if(!output.is_open()) {
            std::cerr << "Eroare la crearea fișierului de ieșire: " << output_filename << std::endl;
            continue;
        }

        // Scrie cuvintele sortate în fisierul de ieșire
        for(const auto& entry : words) {
            output << entry.first << ":[";
            size_t count = 0; 
            for(auto it = entry.second.begin(); it != entry.second.end(); ++it, ++count) {
                output << *it;
                if(count < entry.second.size() - 1)
                    output << " ";
            }
            output << "]\n";
        }

        output.close();
    }
}


int main(int argc, char** argv) {
    try {
        // Verificare argumente
        InputArgs args = parseInputArgs(argc, argv);
        int num_mappers = args.num_mappers;
        int num_reducers = args.num_reducers;
        std::string input_file = args.input_file;
        
        // Deschide fișierul de intrare
        std::ifstream input(input_file);
        if(!input.is_open()) {
            std::cerr << "Eroare la deschiderea fișierului de intrare: " << input_file << std::endl;
            return EXIT_FAILURE;
        }

        // Citeste numarul de fisiere
        int num_files;
        input >> num_files;
        if(input.fail()) {
            std::cerr << "Eroare la citirea numărului de fișiere." << std::endl;
            return EXIT_FAILURE;
        }

        // Citește numele fisierelor
        ThreadSafeFilesQueue queue;
        for(int i = 0; i < num_files; i++) {
            std::string file_name;
            input >> file_name;
            if(input.fail()) {
                std::cerr << "Eroare la citirea numelui fișierului." << std::endl;
                return EXIT_FAILURE;
            }
            queue.addFile(file_name);
        }
        input.close();

        // Initializeaza reducerii
        std::vector<std::unique_ptr<ReducerData>> reducers;
        for(int i = 0; i < num_reducers; i++) {
            reducers.push_back(std::make_unique<ReducerData>());
        }

        // Controlul mapping-ului
        MappingControl control;

        // Creează thread pool pentru mappers
        ThreadPool mapper_pool(num_mappers);
        std::vector<std::future<void>> mapper_futures;

        // Lanseaza mapper threads
        for(int i = 0; i < num_mappers; i++) {
            mapper_futures.push_back(mapper_pool.enqueue([&]() {
                mapperFunction(queue, reducers, num_reducers);
            }));
        }

        // Așteapta finalizarea mapper threads
        for(auto& future : mapper_futures) {
            future.wait();
        }

        // Semnaleaza ca mapping-ul s-a terminat
        control.setDone();

        // Configurarea reducerilor
        std::vector<std::thread> reducer_threads;
        std::vector<std::vector<char>> reducer_letter_assignments(num_reducers);

        // Asignează literele alfabetului către Reduceri cât mai echilibrat
        int letters_per_reducer = ALPHABET_SIZE / num_reducers;
        int extra_letters = ALPHABET_SIZE % num_reducers;
        int current_letter = 0;

        for(int i = 0; i < num_reducers; i++) {
            int assigned_letters = letters_per_reducer + (i < extra_letters ? 1 : 0);
            for(int j = 0; j < assigned_letters && current_letter < ALPHABET_SIZE; j++, current_letter++) {
                char letter = 'a' + current_letter;
                reducer_letter_assignments[i].push_back(letter);
            }
        }

        // Creează thread-urile Reducer
        for(int i = 0; i < num_reducers; i++) {
            reducer_threads.emplace_back(reducerFunction, 
                                         reducer_letter_assignments[i], 
                                         reducers[i].get(), 
                                         std::ref(control));
        }

        // Așteaptă finalizarea thread-urilor Reducer
        for(auto& thread : reducer_threads) {
            thread.join();
        }

    

        std::cout << "Procesarea a fost finalizată cu succes." << std::endl;

        return EXIT_SUCCESS;
    }
    catch(const std::exception& e) {
        std::cerr << "Eroare în main: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}