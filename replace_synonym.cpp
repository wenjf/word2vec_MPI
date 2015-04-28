/*
 *
 * 
 * */
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <memory>
using namespace std;

struct vocab_word{
  string word;
  string cnt;
  vocab_word(string s1, string s2){
    word = s1;
    cnt = s2;
  }
  vocab_word(){
  }
};
vector<vocab_word> vocab;//¿ÉÒÔÊ¹ÓÃcppµÄÈÝÆ÷Âï¡£ÕâÃ´ºÃµÄÓ¦ÓÃ³¡¾° vector container get
const int vocab_hash_size = 10000000;
int vocab_hash[vocab_hash_size];
float *syn0;
unsigned long long vocab_size = 0;

int getWordHash(string word){
  unsigned long long a, hash =0;
  int len = word.length();
  for( a<0 ; a<len ; a++) hash = hash *257 + word[a];
  hash = hash % vocab_hash_size;
  return hash;
}

int getWordHash_c(const char *word) {
  unsigned long long a, hash = 0;
  for (a = 0; a < strlen(word); a++) hash = hash * 257 + word[a];
  hash = hash % vocab_hash_size;
  return hash;
}

int readOneWord(string line)
{
  int word_end = line.find(' ');
  string word = line.substr(0, word_end);
  string cnt =  line.substr(word_end+1, line.length() - word_end ).c_str();
  int its_hash = getWordHash_c(word.c_str());
  while(vocab_hash[its_hash]!=-1) its_hash = (its_hash + 1) % vocab_hash_size;
  //printf("%s's hash is %d\n", word.c_str(), its_hash);
  //sleep(2);
  vocab_hash[its_hash] = vocab_size;
  vocab_word word_here(word, cnt);
  vocab.push_back(word_here);
  vocab_size++;
  //printf( "word:%s hash:%d cnt:%s vocab:%lld \n", word.c_str(), its_hash, cnt.c_str(), vocab_size);
  return vocab_size;
}

int getWordCount(char *path){
  for(int a = 0; a < vocab_hash_size; a++ ) vocab_hash[a] = -1;
  ifstream f_count;
  f_count.open( path, ifstream::in );
  string line;
  while( getline(f_count, line) ){
    readOneWord(line);
  }
  printf("load word count file successfully!\n");
}

void readVector( char *path){
  unsigned long long num_word;
  //int  num_word;
  long long  size_vector;
  char ch;
  char word[50];
  int word_hash;
  FILE *f_vec = fopen(path, "rb");
  fscanf(f_vec, "%lld", &num_word);
  fscanf(f_vec, "%lld", &size_vector);
  fscanf(f_vec, "%lld %lld", &num_word, &size_vector);
  printf("number of word: %lld , vocab size:%lld \n", num_word, vocab_size);
  printf("word vector's size is: %lld\n", size_vector);
  if(num_word != vocab_size ){
    printf("number of word in vector file is not equal to vocab_size!!!\n");
    exit(0);}
  syn0 = (float *)malloc(sizeof(float)*num_word*size_vector);
  if( syn0 == NULL){
    printf("memory error!\n");
    exit(0);
  }else
    printf("memory request is OK!\n");
  long long position =0;
  while( !feof(f_vec)){
    fscanf(f_vec, "%s%c", &word, &ch);
    word_hash = getWordHash_c(word);
    //printf("position in vocab is: %d\n", vocab_hash[word_hash]);
    while(vocab[vocab_hash[word_hash]].word != word) {
      word_hash = (word_hash + 1)%vocab_hash_size;
      //printf("hash in vocab is: %d\n", word_hash);
    }
    position = vocab_hash[ word_hash];
    for( int j = 0 ; j < size_vector ; j++){
      fread(&syn0[position*size_vector + j], sizeof(float), 1, f_vec);
      //printf("this %d element is: %f\n", j, syn0[position*size_vector + j]);
    }
  }
  printf("load words' vector successful!\n");
  fclose(f_vec); 
}

void replace_low(char *path){

}

int main( int argc, char **argv ){
  getWordCount(argv[1]);
  readVector(argv[2]);
}

