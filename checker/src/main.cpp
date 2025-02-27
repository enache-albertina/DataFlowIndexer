#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <pthread.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>

// Constante
#define ALPHABET_SIZE 26

// Structură pentru coada de fișiere
struct FilesQueue {
    std::vector<std::string> files_name;
    size_t current_index; // Modificat din int în size_t
    pthread_mutex_t mutex;

    FilesQueue() : current_index(0) {
        pthread_mutex_init(&mutex, NULL);
    }

    ~FilesQueue() {
        pthread_mutex_destroy(&mutex);
    }

    bool getNextFile(std::string &file_name, int &file_id) {
        pthread_mutex_lock(&mutex);
        if(current_index < files_name.size()) {
            file_id = current_index + 1; // ID începe de la 1
            file_name = files_name[current_index];
            current_index++;
            pthread_mutex_unlock(&mutex);
            return true;
        }
        pthread_mutex_unlock(&mutex);
        return false;
    }
};

// Structură pentru reducer data
struct ReducerData {
    std::map<std::string, std::set<int>> word_map;
    pthread_mutex_t mutex;

    ReducerData() {
        pthread_mutex_init(&mutex, NULL);
    }

    ~ReducerData() {
        pthread_mutex_destroy(&mutex);
    }
};

// Structură globală pentru toți Reducer-ii
struct GlobalData {
    std::vector<ReducerData*> reducers;
    int num_reducers;

    GlobalData(int r) : num_reducers(r) {
        for(int i = 0; i < num_reducers; i++) {
            reducers.push_back(new ReducerData());
        }
    }

    ~GlobalData() {
        for(auto rd : reducers) {
            delete rd;
        }
    }
};

// Structură pentru argumentele Mapper-ului
struct MapperArgs {
    GlobalData* global;
    FilesQueue* queue;
};

// Structură pentru reducer task
struct ReducerTask {
    std::vector<char> letters; // Literele pe care le procesează acest Reducer
    ReducerData* data;
    struct MappingControl* control;   // Pointer către MappingControl
};

// Structură pentru controlul mapping-ului
struct MappingControl {
    bool mapping_done;
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    MappingControl() : mapping_done(false) {
        pthread_mutex_init(&mutex, NULL);
        pthread_cond_init(&cond, NULL);
    }

    ~MappingControl() {
        pthread_mutex_destroy(&mutex);
        pthread_cond_destroy(&cond);
    }

    void setDone() {
        pthread_mutex_lock(&mutex);
        mapping_done = true;
        pthread_cond_broadcast(&cond);
        pthread_mutex_unlock(&mutex);
    }

    void waitForDone() {
        pthread_mutex_lock(&mutex);
        while(!mapping_done) {
            pthread_cond_wait(&cond, &mutex);
        }
        pthread_mutex_unlock(&mutex);
    }
};

// Structură care grupează toate datele necesare pentru thread-uri
struct ProgramData {
    GlobalData* global;
    FilesQueue* queue;
    MappingControl* control;

    ProgramData(GlobalData* g, FilesQueue* q, MappingControl* c) : global(g), queue(q), control(c) {}
};

// Funcție pentru normalizarea cuvintelor
std::string normalizeWord(const std::string& word) {
    std::string normalized;
    for(char c : word) {
        if(std::isalpha(static_cast<unsigned char>(c))) {
            normalized += std::tolower(static_cast<unsigned char>(c));
        }
    }
    return normalized;
}

// Comparator pentru sortarea cuvintelor: descrescător după numărul de fișiere, apoi alfabetic
bool compareWords(const std::pair<std::string, std::set<int>>& a, const std::pair<std::string, std::set<int>>& b) {
    if(a.second.size() != b.second.size()) {
        return a.second.size() > b.second.size(); // Descrescător după numărul de fișiere
    }
    return a.first < b.first; // Alfabetic
}

// Funcția Mapper
void* mapperFunction(void* arg) {
    MapperArgs* args = static_cast<MapperArgs*>(arg);
    GlobalData* global = args->global;
    FilesQueue* queue = args->queue;

    std::string file_name;
    int file_id;

    while(queue->getNextFile(file_name, file_id)) {
        std::ifstream file(file_name);
        if(!file.is_open()) {
            std::cerr << "Error opening file: " << file_name << std::endl;
            continue; // Treci la următorul fișier
        }

        std::string word;
        std::set<std::string> unique_words; // Pentru a stoca cuvinte unice din acest fișier
        while(file >> word) {
            std::string normalized = normalizeWord(word);
            if(normalized.empty()) continue;
            unique_words.insert(normalized);
        }
        file.close();

        // Pentru fiecare cuvânt unic, atribuie-l unui Reducer
        for(auto& w : unique_words) {
            char first_letter = w[0];
            if(first_letter < 'a' || first_letter > 'z') continue; // Ignoră cuvintele care nu încep cu litere mici
            // Determină Reducer-ul responsabil pentru această literă
            int letter_pos = first_letter - 'a';
            int letters_per_reducer = ALPHABET_SIZE / global->num_reducers;
            int extra_letters = ALPHABET_SIZE % global->num_reducers;
            int reducer_index = 0;
            int cumulative_letters = 0;
            for(int i = 0; i < global->num_reducers; i++) {
                int current_reducer_letters = letters_per_reducer + (i < extra_letters ? 1 : 0);
                if(letter_pos < cumulative_letters + current_reducer_letters) {
                    reducer_index = i;
                    break;
                }
                cumulative_letters += current_reducer_letters;
            }
            ReducerData* reducer = global->reducers[reducer_index];
            // Blochează mutex-ul Reducer-ului
            pthread_mutex_lock(&reducer->mutex);
            reducer->word_map[w].insert(file_id);
            pthread_mutex_unlock(&reducer->mutex);
        }
    }

    pthread_exit(NULL);
}

// Funcția Reducer
void* reducerFunction(void* arg) {
    ReducerTask* task = static_cast<ReducerTask*>(arg);
    std::vector<char> letters = task->letters;
    ReducerData* data = task->data;
    MappingControl* control = task->control;

    // Așteaptă finalizarea mapping-ului
    control->waitForDone();

    // Pentru fiecare literă atribuită acestui Reducer
    for(auto letter : letters) {
        std::vector<std::pair<std::string, std::set<int>>> words;
        // Colectează cuvintele care încep cu această literă
        pthread_mutex_lock(&data->mutex);
        for(auto& entry : data->word_map) {
            if(entry.first[0] == letter) {
                words.push_back(entry);
            }
        }
        pthread_mutex_unlock(&data->mutex);

        if(words.empty()) continue; // Nu există cuvinte pentru această literă

        // Sortează cuvintele conform cerințelor
        std::sort(words.begin(), words.end(), compareWords);

        // Creează fișierul de ieșire pentru această literă
        std::string output_filename = std::string(1, letter) + ".txt";
        std::ofstream output(output_filename);
        if(!output.is_open()) {
            std::cerr << "Error creating output file: " << output_filename << std::endl;
            continue;
        }

        // Scrie cuvintele sortate în fișierul de ieșire
        for(auto& entry : words) {
            output << entry.first << ":[";
            size_t count = 0; 
            for(auto it = entry.second.begin(); it != entry.second.end(); ++it, ++count) {
                output << *it;
                if(count < entry.second.size() -1)
                    output << " ";
            }
            output << "]\n";
        }

        output.close();
    }

    pthread_exit(NULL);
}

int main(int argc, char** argv) {
    if(argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <num_mappers> <num_reducers> <input_file>" << std::endl;
        return EXIT_FAILURE;
    }

    int num_mappers = std::stoi(argv[1]);
    int num_reducers = std::stoi(argv[2]);
    std::string input_file = argv[3];

    // Deschide fișierul de intrare
    std::ifstream input(input_file);
    if(!input.is_open()) {
        std::cerr << "Error opening input file: " << input_file << std::endl;
        return EXIT_FAILURE;
    }

    // Citește numărul de fișiere
    int num_files;
    input >> num_files;
    if(input.fail()) {
        std::cerr << "Error reading number of files." << std::endl;
        return EXIT_FAILURE;
    }

    // Citește numele fișierelor
    FilesQueue queue;
    for(int i = 0; i < num_files; i++) {
        std::string file_name;
        input >> file_name;
        if(input.fail()) {
            std::cerr << "Error reading file name." << std::endl;
            return EXIT_FAILURE;
        }
        queue.files_name.push_back(file_name);
    }

    input.close();

    // Inițializează GlobalData
    GlobalData global(num_reducers);

    // Inițializează MappingControl
    MappingControl control;

    // Creează structura ProgramData
    ProgramData program_data(&global, &queue, &control);

    // Creează thread-urile Mapper
    std::vector<pthread_t> mappers(num_mappers);
    MapperArgs mapper_args;
    mapper_args.global = program_data.global;
    mapper_args.queue = program_data.queue;

    for(int i = 0; i < num_mappers; i++) {
        if(pthread_create(&mappers[i], NULL, mapperFunction, &mapper_args) != 0) {
            std::cerr << "Error creating mapper thread." << std::endl;
            return EXIT_FAILURE;
        }
    }

    // Asignează literele alfabetului către Reduceri cât mai echilibrat
    std::vector<ReducerTask> reducer_tasks(num_reducers);
    int letters_per_reducer = ALPHABET_SIZE / num_reducers;
    int extra_letters = ALPHABET_SIZE % num_reducers;
    int current_letter = 0;

    for(int i = 0; i < num_reducers; i++) {
        int assigned_letters = letters_per_reducer + (i < extra_letters ? 1 : 0);
        for(int j = 0; j < assigned_letters && current_letter < ALPHABET_SIZE; j++, current_letter++) {
            char letter = 'a' + current_letter;
            reducer_tasks[i].letters.push_back(letter);
        }
        reducer_tasks[i].data = program_data.global->reducers[i];
        reducer_tasks[i].control = program_data.control;
    }

    // Creează thread-urile Reducer
    std::vector<pthread_t> reducers(num_reducers);
    for(int i = 0; i < num_reducers; i++) {
        if(pthread_create(&reducers[i], NULL, reducerFunction, &reducer_tasks[i]) != 0) {
            std::cerr << "Error creating reducer thread." << std::endl;
            return EXIT_FAILURE;
        }
    }

    // Așteaptă finalizarea thread-urilor Mapper
    for(int i = 0; i < num_mappers; i++) {
        pthread_join(mappers[i], NULL);
    }

    // Semnalează Reducer-ilor că mapping-ul s-a terminat
    control.setDone();

    // Așteaptă finalizarea thread-urilor Reducer
    for(int i = 0; i < num_reducers; i++) {
        pthread_join(reducers[i], NULL);
    }

    return EXIT_SUCCESS;
}
