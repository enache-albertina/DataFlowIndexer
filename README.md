# Parallel Inverted Index with Map-Reduce

Acest proiect implementează calculul paralel al unui **index inversat** folosind paradigma **Map-Reduce**. 
Indexul inversat permite căutări rapide pentru a determina în ce documente apare un anumit cuvânt. Proiectul
utilizează tehnologia **Pthreads** pentru a implementa algoritmi paraleli și distribuirea sarcinilor între
firele de execuție.

## Scopul Proiectului

Scopul acestui proiect este de a construi un **index inversat paralel** pentru un set de documente folosind tehnica Map-Reduce. Aceasta presupune:

- Procesarea unui set de documente în paralel folosind thread-uri pentru extragerea cuvintelor și crearea listelor parțiale.
- Agregarea acestor liste parțiale și sortarea lor pentru a obține un index inversat complet, care să fie organizat alfabetic.

## Tehnologii Folosite

- **C/C++**: Limbajul de programare folosit pentru implementarea algoritmului.
- **Pthreads**: Biblioteca folosită pentru implementarea algoritmilor paraleli.
- **Map-Reduce**: Paradigma folosită pentru procesarea și agregarea datelor.
- **Docker**: Folosit pentru a crea un mediu uniform de testare.


### Descrierea fișierelor:

- **checker/**: Contine scripturi de testare și fișiere de intrare/ieșire.
- **src/**: Codul sursă al proiectului.
- **Dockerfile**: Configurația pentru crearea imaginii Docker.
- **docker-compose.yml**: Definiția serviciilor pentru Docker.
- **run_with_docker.sh**: Script pentru a porni testele într-un container Docker.
- **test35.sh**: Script pentru rularea automată a testelor multiple.

## Instalare

### 1. **Instalare pe Linux (sau WSL)**

Pentru a instala și rula proiectul local, urmează acești pași:

1. Clonarea repository-ului:
   ```bash
   git clone https://github.com/enache-albertina/API-LibraryManager.git
   cd API-LibraryManager
Compilarea proiectului:

Asigură-te că ai un compilator C instalat (gcc sau clang).
Rulează comanda pentru a compila proiectul:
bash
make
Rularea temei: Pentru a rula tema, folosește comanda:


./tema1 <numar_mapperi> <numar_reduceri> <fisier_intrare>


2. Instalare folosind Docker
Dacă vrei să rulezi testele într-un mediu izolat folosind Docker, poți utiliza următorul script:

Asigură-te că Docker este instalat pe sistemul tău.
Rulează scriptul pentru a crea și porni containerul Docker:

./run_with_docker.sh

Acest script va crea un container Docker, va rula testele și va opri containerul la final.


Verificarea rezultatelor
După rularea testelor, rezultatele vor fi salvate în fișierele din directorul test_out. 
Poți verifica dacă rezultatele sunt corecte folosind scriptul checker.sh:

./checker/checker.sh


### Algoritmul Map-Reduce
Funcția Map:

Fiecare document este asignat unui thread Mapper.
Mapper-ul citește documentul și extrage cuvintele unice, împreună cu ID-ul documentului și poziția cuvântului.
Fiecare Mapper generează o listă parțială de perechi {cuvânt, ID document, poziție}.
Funcția Reduce:

Reducer-ul combină listele parțiale generate de Mappers.
Cuvintele sunt grupate după litera cu care încep și sortate descrescător după numărul de documente în care apar.
Testare
Pentru a verifica corectitudinea și scalabilitatea implementării, rulează mai multe instanțe de Mapper
și Reducer folosind variabilele de comandă numar_mapperi și numar_reduceri. Programul va crea fișierele
de ieșire pentru fiecare literă a alfabetului, sortate după frecvența cuvintelor.
