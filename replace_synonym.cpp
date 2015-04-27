/*
 *
 * 
 * */
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <cstring>
using namespace std;

struct vocab_word{
  string word;
  string cnt;
  vocab_word(string s1, string s2){
    word = s1;
    cnt = s2;
  }
};
vector<vocab_word> vocab;//¿ÉÒÔÊ¹ÓÃcppµÄÈÝÆ÷Âï¡£ÕâÃ´ºÃµÄÓ¦ÓÃ³¡¾° vector container get
const int vocab_hash_size = 10000000;
int vocab_hash[vocab_hash_size];
float *syn0;
long long vocab_size = 0;

int getWordHash(string word){
  unsigned long long a, hash =0;
  int len = word.length();
  for( a<0 ; a<len ; a++) hash = hash *257 + word[a];
  hash = hash % vocab_hash_size;
  return hash;
}

int readOneWord(string line)
{
  int word_end = line.find(' ');
  string word = line.substr(0, word_end);
  string cnt =  line.substr(word_end+1, line.length() - word_end ).c_str();
  int its_hash = getWordHash(word);
  while(vocab_hash[its_hash]!=-1) its_hash = (its_hash + 1) % vocab_hash_size;
  vocab_hash[its_hash] = vocab_size;
  vocab_word word_here = new vocab_word(word.c_str, cnt.c_str());
  vocab.push_back(cnt);
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
}

void readVector( char *path){
  long long num_word;
  int size_vector;
  char ch;
  char word[];//ÁªÏµµ½Ö®Ç°Ð´getWordHashº¯Êý£¬½Ó¿Ú¶¼Ã»ÓÐ¶¨ÒåºÃ£¬µ¼ÖÂÏÖÔÚÐèÒª×ª»»ÀàÐÍÁË
  int word_hash;
  FILE *f_vec = fopen(path, "wb");
  fscanf(f_vec, "%d %d", &num_word, &size_vector);
  if(num_word != vocab_size){
    printf("number of word in vector file is not equal to vocab_size!!!\n");
    exit(0);
  }
  while( !feod(f_vec)){
    fscanf(f_vec, "%s%c", word, ch);
    word_hash = getWordHash(string(word));
    while(vocab_hash[word_hash]!=-1) its_hash = (its_hash + 1) % vocab_hash_size;
  }


}


int main( int argc, char **argv ){
  getWordCount(argv[1]);
}

