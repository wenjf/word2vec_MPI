//  Copyright 2013 Google Inc. All Rights Reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

#define MAX_STRING 100
#define EXP_TABLE_SIZE 1000
#define MAX_EXP 6
#define MAX_SENTENCE_LENGTH 1000
#define MAX_CODE_LENGTH 40

const int vocab_hash_size = 30000000;  // Maximum 30 * 0.7 = 21M words in the vocabulary

typedef float real;                    // Precision of float numbers

struct vocab_word
{
  long long cn;//词频
  int *point;//huffman编码对应内节点的路径
  char *word, *code, codelen;//huffman编码
};

char train_file[MAX_STRING], output_file[MAX_STRING];//
char save_vocab_file[MAX_STRING], read_vocab_file[MAX_STRING];
struct vocab_word *vocab;//定义词表
int binary = 0, cbow = 0, debug_mode = 2, window = 5, min_count = 5, num_threads = 1, min_reduce = 1;//主函数参数
int *vocab_hash;
long long vocab_max_size = 1000, vocab_size = 0, layer1_size = 100;
long long train_words = 0, word_count_actual = 0, file_size = 0, classes = 0;
real alpha = 0.025, starting_alpha, sample = 0;
real *syn0, *syn1, *syn1neg, *expTable;
clock_t start;

int hs = 1, negative = 0;
const int table_size = 1e8;
int *table;


//每个单词的能量分布表，table在负样本抽样中用到
void InitUnigramTable()
{
  int a, i;
  long long train_words_pow = 0;
  real d1, power = 0.75;
  table = (int *)malloc(table_size * sizeof(int));
  for (a = 0; a < vocab_size; a++) //遍历词汇表，统计词的能量总值train_words_pow，指数power应该是缩小值的吧。
	  train_words_pow += pow(vocab[a].cn, power);
  i = 0;
  d1 = pow(vocab[i].cn, power) / (real)train_words_pow;//表示已遍历的词的能量值占总能力值的比例
  for (a = 0; a < table_size; a++)//遍历table。a表示table的位置，i表示词汇表的位置
  {
    table[a] = i;//单词i占用table的a位置
    //table反映的是一个单词能量的分布，一个单词能量越大，所占用的table的位置越多
    if (a / (real)table_size > d1)
    {
      i++;//移动到下一个词
      d1 += pow(vocab[i].cn, power) / (real)train_words_pow;
    }
    if (i >= vocab_siInitNetze) i = vocab_size - 1;
  }
}

// Reads a single word from a file, assuming space + tab + EOL to be word boundaries
//从文件中读取一个词
void ReadWord(char *word, FILE *fin) {
  int a = 0, ch;
  while (!feof(fin)) {
    ch = fgetc(fin);
    if (ch == 13) continue;
    if ((ch == ' ') || (ch == '\t') || (ch == '\n')) {
      if (a > 0) {
        if (ch == '\n') ungetc(ch, fin);
        break;
      }
      if (ch == '\n') {
        strcpy(word, (char *)"</s>");
        return;
      } else continue;
    }
    word[a] = ch;
    a++;
    if (a >= MAX_STRING - 1) a--;   // Truncate too long words
  }
  word[a] = 0;//C语言中char*以0结尾
}

// Returns hash value of a word返回一个词的hash值，一个词跟hash值一一对应（可能冲突）
int GetWordHash(char *word)
{
  unsigned long long a, hash = 0;
  for (a = 0; a < strlen(word); a++)
	  hash = hash * 257 + word[a];//采取257进制
  hash = hash % vocab_hash_size;
  return hash;
}

// Returns position of a word in the vocabulary; if the word is not found, returns -1
// 返回一个词在词汇表中的位置，如果不存在则返回-1
int SearchVocab(char *word)
{
  unsigned int hash = GetWordHash(word);
  while (1)
  {
    if (vocab_hash[hash] == -1) return -1;
    if (!strcmp(word, vocab[vocab_hash[hash]].word))//如果是这个word，返回在此表中的位置，否则使用线性开放寻址法，
    	return vocab_hash[hash];
    hash = (hash + 1) % vocab_hash_size;
  }
  return -1;
}

// Reads a word and returns its index in the vocabulary
// 从文件流中读取一个词，并返回这个词在词汇表中的位置
int ReadWordIndex(FILE *fin)
{
  char word[MAX_STRING];
  ReadWord(word, fin);//与下一行的关系
  if (feof(fin)) return -1;
  return SearchVocab(word);
}

// Adds a word to the vocabulary 将一个词添加到一个词汇中
int AddWordToVocab(char *word)
{
  unsigned int hash, length = strlen(word) + 1;
  if (length > MAX_STRING)
	  length = MAX_STRING;//首先约束这个词的长度.
  vocab[vocab_size].word = (char *)calloc(length, sizeof(char));//分配内存
  strcpy(vocab[vocab_size].word, word);//对上一行分配的内存赋值
  vocab[vocab_size].cn = 0;//初始化这个词的词频
  vocab_size++;//词表的长度+1
  // Reallocate memory if needed
  if (vocab_size + 2 >= vocab_max_size)
  {
    vocab_max_size += 1000;
    vocab = (struct vocab_word *)realloc(vocab, vocab_max_size * sizeof(struct vocab_word));
  }//管理词表的内存
  hash = GetWordHash(word);//求这个词的hash值
  while (vocab_hash[hash] != -1)//如果hash值冲突了
	  hash = (hash + 1) % vocab_hash_size;//使用开放地址法解决冲突
  vocab_hash[hash] = vocab_size - 1;//由词的hash值找到她所在词汇表的排序位置 
  return vocab_size - 1;//返回这个词在*vocab中的位置
}

// Used later for sorting by word counts
int VocabCompare(const void *a, const void *b)
{
    return ((struct vocab_word *)b)->cn - ((struct vocab_word *)a)->cn;//attention here, 指针的强制转换
}

// Sorts the vocabulary by frequency using word counts
// 根据词频排序
void SortVocab()
{
  int a, size;
  unsigned int hash;
  // Sort the vocabulary and keep </s> at the first position
  qsort(&vocab[1], vocab_size - 1, sizeof(struct vocab_word), VocabCompare);//需了解C语言中的快排
  for (a = 0; a < vocab_hash_size; a++)
	  vocab_hash[a] = -1;//没懂这句
  size = vocab_size;//vocab_size全局变量,表示*vocab的长度
  train_words = 0;
  for (a = 0; a < size; a++)
  {
    // Words occuring less than min_count times will be discarded from the vocab
	//出现太少的词直接丢弃
    if (vocab[a].cn < min_count)
    {
      vocab_size--;
      free(vocab[vocab_size].word);
    }
    else
    {
      // Hash will be re-computed, as after the sorting it is not actual
      // 重新计算hash查找。vocab_hash是由hash值找到该词所在位置
      hash=GetWordHash(vocab[a].word);
      while (vocab_hash[hash] != -1) hash = (hash + 1) % vocab_hash_size;
      vocab_hash[hash] = a;
      train_words += vocab[a].cn;
    }
  }
  vocab = (struct vocab_word *)realloc(vocab, (vocab_size + 1) * sizeof(struct vocab_word));
  // Allocate memory for the binary tree construction
  for (a = 0; a < vocab_size; a++)
  {
    vocab[a].code = (char *)calloc(MAX_CODE_LENGTH, sizeof(char));
    vocab[a].point = (int *)calloc(MAX_CODE_LENGTH, sizeof(int));
  }
}

// Reduces the vocabulary by removing infrequent tokens
// 再次移除词频过小的词，缩减词汇表
void ReduceVocab()
{
  int a, b = 0;
  unsigned int hash;
  for (a = 0; a < vocab_size; a++)//我草，这很容易看错啊
	if (vocab[a].cn > min_reduce)
	{
		vocab[b].cn = vocab[a].cn;
		vocab[b].word = vocab[a].word;
		b++;
	}
	else 
		free(vocab[a].word);
  
  vocab_size = b;//更新此表长度
  
  for (a = 0; a < vocab_hash_size; a++) 
	  vocab_hash[a] = -1;
  
  for (a = 0; a < vocab_size; a++) {
    // Hash will be re-computed, as it is not actual
    hash = GetWordHash(vocab[a].word);
    while (vocab_hash[hash] != -1) hash = (hash + 1) % vocab_hash_size;
    vocab_hash[hash] = a;
  }
  fflush(stdout);//冲洗写文件缓冲区
  min_reduce++;
}

// Create binary Huffman tree using the word counts根据词频创建huffman树
// Frequent words will have short uniqe binary codes词频越大的单词有越短的huffman编码
void CreateBinaryTree() {
  long long a, b, i, min1i, min2i, pos1, pos2, point[MAX_CODE_LENGTH];
  char code[MAX_CODE_LENGTH];
  long long *count = (long long *)calloc(vocab_size * 2 + 1, sizeof(long long));//为啥这么定义？
  long long *binary = (long long *)calloc(vocab_size * 2 + 1, sizeof(long long));//同上？
  long long *parent_node = (long long *)calloc(vocab_size * 2 + 1, sizeof(long long));//
  for (a = 0; a < vocab_size; a++) 
	  count[a] = vocab[a].cn;
  for (a = vocab_size; a < vocab_size * 2; a++) 
	  count[a] = 1e15;//什么鬼？
  
  pos1 = vocab_size - 1;
  pos2 = vocab_size;
  // Following algorithm constructs the Huffman tree by adding one node at a time
  for (a = 0; a < vocab_size - 1; a++)
  { 
    // First, find two smallest nodes 'min1, min2' 找出目前权值最小的两个节点
    if (pos1 >= 0)//第一个权值最小的节点
    {
      if (count[pos1] < count[pos2])
      {
        min1i = pos1;
        pos1--;
      }
      else
      {
        min1i = pos2;
        pos2++;
      }
    }
    else
    {
      min1i = pos2;
      pos2++;
    }
    if (pos1 >= 0)//第二个权值最小的节点
    {
      if (count[pos1] < count[pos2])
      {
        min2i = pos1;
        pos1--;
      }
      else
      {
        min2i = pos2;
        pos2++;
      }
    } 
    else
    {
      min2i = pos2;
      pos2++;
    }
	
    count[vocab_size + a] = count[min1i] + count[min2i];//更新权值
    parent_node[min1i] = vocab_size + a;//更新父节点
    parent_node[min2i] = vocab_size + a;//更新父节点
    binary[min2i] = 1;//节点编码为1，之前默认是0。word总在0的子节点这边
  }
  
  // Now assign binary code to each vocabulary word
  for (a = 0; a < vocab_size; a++)
  {
    b = a;
    i = 0;
    while (1)
    {
      code[i] = binary[b];//code的编码是从叶子到根的
      point[i] = b;
      i++;
      b = parent_node[b];
      if (b == vocab_size * 2 - 2) break;//根节点
    }
    vocab[a].codelen = i;//编码长度
    vocab[a].point[0] = vocab_size - 2;//这个
    for (b = 0; b < i; b++)
    {
      vocab[a].code[i - b - 1] = code[b];
      vocab[a].point[i - b] = point[b] - vocab_size;
    }
  }
  //释放局部变量内存
  free(count);
  free(binary);
  free(parent_node);
}

//从分词文件中统计每个单词的词频
void LearnVocabFromTrainFile()
{
  char word[MAX_STRING];
  FILE *fin;
  long long a, i;
  for (a = 0; a < vocab_hash_size; a++) vocab_hash[a] = -1;//所有的哈希值对应的单词位置初始化为-1，后面判断hash值是否冲突会用到
  fin = fopen(train_file, "rb");
  if (fin == NULL)
  {
    printf("ERROR: training data file not found!\n");
    exit(1);
  }//没有读取到文件则退出，应该有这种判断，学习了
  vocab_size = 0;
  AddWordToVocab((char *)"</s>");//预处理之将</s>放在*vocab的第一个
  while (1)
  {
    ReadWord(word, fin);
    if (feof(fin)) break;//只有读完文件才break
    train_words++;//没读一个word，记录一下
    if ((debug_mode > 1) && (train_words % 100000 == 0))
    {
      printf("%lldK%c", train_words / 1000, 13);
      fflush(stdout);
    }//debug_mode 实在没懂
    i = SearchVocab(word);//返回该词在词汇表中的位置
    if (i == -1)//该词之前不存在
    {
      a = AddWordToVocab(word);//把该词添加到词汇表中
      vocab[a].cn = 1;
    }
    else vocab[i].cn++;//更新词频
    if (vocab_size > vocab_hash_size * 0.7)//如果词汇表太庞大，就缩减;评判依据是什么呢？
    	ReduceVocab();
  }//训练文件读取完毕
  
  SortVocab();//根据词频排序词汇表
  if (debug_mode > 0)
  {
    printf("Vocab size: %lld\n", vocab_size);
    printf("Words in train file: %lld\n", train_words);
  }
  file_size = ftell(fin);
  fclose(fin);
}

void SaveVocab() {
  long long i;
  FILE *fo = fopen(save_vocab_file, "wb");
  for (i = 0; i < vocab_size; i++) fprintf(fo, "%s %lld\n", vocab[i].word, vocab[i].cn);
  fclose(fo);//将词汇表写入文件
}

//从文件读取词汇，该文件已经统计好了每个词汇的词频
void ReadVocab()
{
  long long a, i = 0;
  char c;
  char word[MAX_STRING];
  FILE *fin = fopen(read_vocab_file, "rb");//打开词汇文件
  if (fin == NULL)
  {
    printf("Vocabulary file not found\n");
    exit(1);
  }
  for (a = 0; a < vocab_hash_size; a++)//vocab_hash初始化，未使用的hash值对应-1
	  vocab_hash[a] = -1;
  vocab_size = 0;//初始化 vocab_size 为 0 
  while (1)
  {
    ReadWord(word, fin);//从fin进入一个词到word中
    if (feof(fin)) break;//读完了就退出
    a = AddWordToVocab(word);//把该词添加到词汇表中，并返回该词在此表中的位置
    fscanf(fin, "%lld%c", &vocab[a].cn, &c);//读取词频？c是干啥的吗，读取空格吗   暂时可以当看懂了
    i++;
  }
  SortVocab();//根据词频排序???不是已经排序了？难道是为了复用？
  if (debug_mode > 0)//debug model 之后再看
  {
    printf("Vocab size: %lld\n", vocab_size);
    printf("Words in train file: %lld\n", train_words);
  }

  //读取训练数据
  fin = fopen(train_file, "rb");
  if (fin == NULL)
  {
    printf("ERROR: training data file not found!\n");
    exit(1);
  }
  fseek(fin, 0, SEEK_END);//文件指针指向 SEEK_END 为基准，偏移为0的位置。这里好像就是 直接到文件末尾，具体功能之后看
  file_size = ftell(fin);//根据指针计算文件大小。
  fclose(fin);
}

void InitNet()//
{
  long long a, b;
  a = posix_memalign((void **)&syn0, 128, (long long)vocab_size * layer1_size * sizeof(real));
  //先知道这个也是申请动态数组，对齐还有128这个参数以后再了解
  if (syn0 == NULL)
  {
	  printf("Memory allocation failed\n"); exit(1);
  }
  if (hs)//采用softmax hirarchical softmax
  {
    a = posix_memalign((void **)&syn1, 128, (long long)vocab_size * layer1_size * sizeof(real));
    if (syn1 == NULL)
    {
    	printf("Memory allocation failed\n"); exit(1);
    }
    for (b = 0; b < layer1_size; b++)
    	for (a = 0; a < vocab_size; a++)
    		syn1[a * layer1_size + b] = 0;//为什么要初始化成0呢？
  }
  if (negative>0)//还有负样本
  {
    a = posix_memalign((void **)&syn1neg, 128, (long long)vocab_size * layer1_size * sizeof(real));
    if (syn1neg == NULL)
    {
    	printf("Memory allocation failed\n"); exit(1);
    }
    for (b = 0; b < layer1_size; b++)
    	for (a = 0; a < vocab_size; a++)
    		syn1neg[a * layer1_size + b] = 0;
  }
  
  for (b = 0; b < layer1_size; b++)
	  for (a = 0; a < vocab_size; a++)
		  syn0[a * layer1_size + b] = (rand() / (real)RAND_MAX - 0.5) / layer1_size;//初始化向量！！！找到了！
  CreateBinaryTree();//建立huffman树，对每个单词进行编码
}


//这个线程函数执行之前，已经做好了一些工作：根据词频排序的词汇表，每个单词的huffman编码
void *TrainModelThread(void *id)
{
  long long a, b, d, word, last_word, sentence_length = 0, sentence_position = 0;
  long long word_count = 0, last_word_count = 0, sen[MAX_SENTENCE_LENGTH + 1];
  long long l1, l2, c, target, label;
  unsigned long long next_random = (long long)id;
  real f, g;
  clock_t now;
  real *neu1 = (real *)calloc(layer1_size, sizeof(real));
  real *neu1e = (real *)calloc(layer1_size, sizeof(real));
  FILE *fi = fopen(train_file, "rb");
  //每个线程对应一段文本。根据线程id找到自己负责的文本的初始位置
  fseek(fi, file_size / (long long)num_threads * (long long)id, SEEK_SET);//SEEK_SET文件开头
  while (1)
  {
    if (word_count - last_word_count > 10000)
    {
      word_count_actual += word_count - last_word_count;
      last_word_count = word_count;
      if ((debug_mode > 1))
      {
        now=clock();
        printf("%cAlpha: %f  Progress: %.2f%%  Words/thread/sec: %.2fk  ", 13, alpha,
         word_count_actual / (real)(train_words + 1) * 100,
         word_count_actual / ((real)(now - start + 1) / (real)CLOCKS_PER_SEC * 1000));
        fflush(stdout);//强制写入文件
      }
      alpha = starting_alpha * (1 - word_count_actual / (real)(train_words + 1));//更新步长
      if (alpha < starting_alpha * 0.0001) alpha = starting_alpha * 0.0001;
    }
	
    if (sentence_length == 0)
    {
      while (1)
      {
        word = ReadWordIndex(fi);//从文件流中读取一个词，并返回这个词在词汇表中的位置
        if (feof(fi)) break;
        if (word == -1) continue;//是否在词表中，若不在，跳过
        word_count++;
        if (word == 0) break;
        // The subsampling randomly discards frequent words while keeping the ranking same
        if (sample > 0)//对高频词进行下采样，不过要保持排序不变。
        {
          real ran = (sqrt(vocab[word].cn / (sample * train_words)) + 1) * (sample * train_words) / vocab[word].cn;
          next_random = next_random * (unsigned long long)25214903917 + 11;
          if (ran < (next_random & 0xFFFF) / (real)65536) continue;
        }//没太懂这个亚采样到底是怎么采样的
        sen[sentence_length] = word;
        sentence_length++;
        //1000个单词视作一个句子. 作为缓存，来考虑上下文
        if (sentence_length >= MAX_SENTENCE_LENGTH) break;
      }
      sentence_position = 0;//从句子第一个词开始处理
    }
	
    if (feof(fi)) break;//这个合理吗？
    if (word_count > train_words / num_threads) break;//如果当前线程已处理的单词超过了 阈值，则退出。
    word = sen[sentence_position];//读句子中当前词
    if (word == -1) continue;//防止之前有错
    for (c = 0; c < layer1_size; c++) neu1[c] = 0;//初始化上下文平均向量
    for (c = 0; c < layer1_size; c++) neu1e[c] = 0;//这个又是啥？？？
    next_random = next_random * (unsigned long long)25214903917 + 11;
    b = next_random % window;
    if (cbow)
    {  //train the cbow architecture
      // in -> hidden
      for (a = b; a < window * 2 + 1 - b; a++) if (a != window)//扫描目标单词的左右几个单词
      {
        c = sentence_position - window + a;//c代表窗口内的当前考虑的这个词，不是目标词
        if (c < 0) continue;
        if (c >= sentence_length) continue;
        last_word = sen[c];
        if (last_word == -1) continue;
        for (c = 0; c < layer1_size; c++)//layer1_size词向量的维度，默认值是100
        	neu1[c] += syn0[c + last_word * layer1_size];//传说中的向量和？
      }//为什么要搞个这么个随机数
      if (hs) 
		for (d = 0; d < vocab[word].codelen; d++)//开始遍历huffman树，每次一个节点   vocab[word].codelen代表word的编码长度
		{
			f = 0;
			l2 = vocab[word].point[d] * layer1_size;//point应该记录的是huffman的路径。找到当前节点，并算出
			// Propagate hidden -> output
			for (c = 0; c < layer1_size; c++) f += neu1[c] * syn1[c + l2];//计算内积
			if (f <= -MAX_EXP) continue;//内积不在范围内直接丢弃
			else if (f >= MAX_EXP) continue;//同上
			else f = expTable[(int)((f + MAX_EXP) * (EXP_TABLE_SIZE / MAX_EXP / 2))];//内积之后sigmoid函数 bingo~
			// 'g' is the gradient multiplied by the learning rate
			g = (1 - vocab[word].code[d] - f) * alpha;//偏导数的一部分

			//layer1_size是向量的维度
			// Propagate errors output -> hidden 反向传播误差，从huffman树传到隐藏层。下面就是把当前内节点的误差传播给隐藏层，syn1[c + l2]是偏导数的一部分。
			for (c = 0; c < layer1_size; c++) neu1e[c] += g * syn1[c + l2];

			// Learn weights hidden -> output 更新当前内节点的向量，后面的neu1[c]其实是偏导数的一部分
			for (c = 0; c < layer1_size; c++) syn1[c + l2] += g * neu1[c];
		}
	
      // NEGATIVE SAMPLING
      if (negative > 0)
      for (d = 0; d < negative + 1; d++)
      {
        if (d == 0)
        {
          target = word;//目标单词
          label = 1;//正样本
        }
        else
        {
          next_random = next_random * (unsigned long long)25214903917 + 11;
          target = table[(next_random >> 16) % table_size];
          if (target == 0) target = next_random % (vocab_size - 1) + 1;
          if (target == word) continue;
          label = 0;//负样本
        }
        l2 = target * layer1_size;
        f = 0;
        for (c = 0; c < layer1_size; c++)
        	f += neu1[c] * syn1neg[c + l2];//内积
        if (f > MAX_EXP)
        	g = (label - 1) * alpha;
        else if (f < -MAX_EXP)
        	g = (label - 0) * alpha;
        else g = (label - expTable[(int)((f + MAX_EXP) * (EXP_TABLE_SIZE / MAX_EXP / 2))]) * alpha;//完全不是一个风格的！
        for (c = 0; c < layer1_size; c++)
        	neu1e[c] += g * syn1neg[c + l2];//隐藏层的误差
        for (c = 0; c < layer1_size; c++)
        	syn1neg[c + l2] += g * neu1[c];//更新负样本向量
      }
      //end of NEGATIVE SAMPLING
	  
	  // hidden -> in
      for (a = b; a < window * 2 + 1 - b; a++)//窗口大小由参数决定
      if (a != window)//cbow模型 更新的不是中间词语的向量，而是周围几个词语的向量。
      {
        c = sentence_position - window + a;
        if (c < 0) continue;
        if (c >= sentence_length) continue;
        last_word = sen[c];
        if (last_word == -1) continue;
        for (c = 0; c < layer1_size; c++)
        	syn0[c + last_word * layer1_size] += neu1e[c];//更新词向量
      }                                                                                                                                        
    }
    else
    {  //train skip-gram
       for (a = b; a < window * 2 + 1 - b; a++)
       if (a != window)//扫描周围几个词语
       {
        c = sentence_position - window + a;
        if (c < 0) continue;
        if (c >= sentence_length) continue;
        last_word = sen[c];
        if (last_word == -1) continue;
        l1 = last_word * layer1_size;
        for (c = 0; c < layer1_size; c++)
        	neu1e[c] = 0;
        // HIERARCHICAL SOFTMAX
        if (hs)
        for (d = 0; d < vocab[word].codelen; d++)//遍历叶子节点
        {
          f = 0;
          l2 = vocab[word].point[d] * layer1_size;//point记录的是huffman的路径
          // Propagate hidden -> output 感觉源代码这个英语注释有点误导人，这里的隐藏层就是输入层，就是词向量。
          for (c = 0; c < layer1_size; c++)
        	  f += syn0[c + l1] * syn1[c + l2];//计算两个词向量的内积
          if (f <= -MAX_EXP) continue;
          else if (f >= MAX_EXP) continue;
          else f = expTable[(int)((f + MAX_EXP) * (EXP_TABLE_SIZE / MAX_EXP / 2))];
          // 'g' is the gradient multiplied by the learning rate
          g = (1 - vocab[word].code[d] - f) * alpha;//偏导数的一部分
          // Propagate errors output -> hidden
          for (c = 0; c < layer1_size; c++)
        	  neu1e[c] += g * syn1[c + l2];//隐藏层的误差
          // Learn weights hidden -> output
          for (c = 0; c < layer1_size; c++)
        	  syn1[c + l2] += g * syn0[c + l1];//更新叶子节点向量
        }
        // NEGATIVE SAMPLING
        if (negative > 0)//这个同cobow差不多
        for (d = 0; d < negative + 1; d++)
        {
          if (d == 0)
          {
            target = word;
            label = 1;
          }
          else
          {
            next_random = next_random * (unsigned long long)25214903917 + 11;
            target = table[(next_random >> 16) % table_size];
            if (target == 0) target = next_random % (vocab_size - 1) + 1;
            if (target == word) continue;
            label = 0;
          }
          l2 = target * layer1_size;
          f = 0;
          for (c = 0; c < layer1_size; c++)
        	  f += syn0[c + l1] * syn1neg[c + l2];
          if (f > MAX_EXP) g = (label - 1) * alpha;
          else if (f < -MAX_EXP)
        	  g = (label - 0) * alpha;
          else g = (label - expTable[(int)((f + MAX_EXP) * (EXP_TABLE_SIZE / MAX_EXP / 2))]) * alpha;
          for (c = 0; c < layer1_size; c++)
        	  neu1e[c] += g * syn1neg[c + l2];
          for (c = 0; c < layer1_size; c++)
        	  syn1neg[c + l2] += g * syn0[c + l1];
        }

        // Learn weights input -> hidden
        for (c = 0; c < layer1_size; c++)
        	syn0[c + l1] += neu1e[c];//更新周围几个词语的向量
      }
    }
    
	sentence_position++;
    if (sentence_position >= sentence_length)
    {
      sentence_length = 0;
      continue;
    }
  }
  fclose(fi);
  free(neu1);
  free(neu1e);
  pthread_exit(NULL);
}

void TrainModel()
{
  long a, b, c, d;
  FILE *fo;
  pthread_t *pt = (pthread_t *)malloc(num_threads * sizeof(pthread_t));
  printf("Starting training using file %s\n", train_file);
  starting_alpha = alpha;
  if (read_vocab_file[0] != 0)//是否有词频文件,如果有
	  ReadVocab();//从文件读入词汇
  else//没有，则
	  LearnVocabFromTrainFile();//从训练文件学习词汇
  if (save_vocab_file[0] != 0)
	  SaveVocab();//保存词汇
  if (output_file[0] == 0)
	  return;
  InitNet();
  if (negative > 0) InitUnigramTable();
  start = clock();
  for (a = 0; a < num_threads; a++) 
	  pthread_create(&pt[a], NULL, TrainModelThread, (void *)a);
  for (a = 0; a < num_threads; a++) 
	  pthread_join(pt[a], NULL);//等待所有的线程结束
  fo = fopen(output_file, "wb");
  if (classes == 0) //不需要聚类，只需要输出词向量
  {
    // Save the word vectors
    fprintf(fo, "%lld %lld\n", vocab_size, layer1_size);
    for (a = 0; a < vocab_size; a++)
    {
      fprintf(fo, "%s ", vocab[a].word);
      if (binary)
    	  for (b = 0; b < layer1_size; b++)
    		  fwrite(&syn0[a * layer1_size + b], sizeof(real), 1, fo);
      else
    	  for (b = 0; b < layer1_size; b++)
    		  fprintf(fo, "%lf ", syn0[a * layer1_size + b]);
      fprintf(fo, "\n");
    }
  }
  else //使用k-means进行聚类
  {
    // Run K-means on the word vectors
    int clcn = classes, iter = 10, closeid;
    int *centcn = (int *)malloc(classes * sizeof(int));//该类别的数量
    int *cl = (int *)calloc(vocab_size, sizeof(int));//词到类别的映射
    real closev, x;
    real *cent = (real *)calloc(classes * layer1_size, sizeof(real));//质心数组
    for (a = 0; a < vocab_size; a++)
    	cl[a] = a % clcn;//任意分类？
    for (a = 0; a < iter; a++)
    {
      for (b = 0; b < clcn * layer1_size; b++)
    	  cent[b] = 0;//质心清零
      for (b = 0; b < clcn; b++)
    	  centcn[b] = 1;
      for (c = 0; c < vocab_size; c++)
      {
        for (d = 0; d < layer1_size; d++)
        	cent[layer1_size * cl[c] + d] += syn0[c * layer1_size + d];//求和放到质心数组中
        centcn[cl[c]]++;//类别数量加1
      }
      for (b = 0; b < clcn; b++)//遍历所有类别
      {
        closev = 0;
        for (c = 0; c < layer1_size; c++)
        {
          cent[layer1_size * b + c] /= centcn[b];//均值，就是求新的质心
          closev += cent[layer1_size * b + c] * cent[layer1_size * b + c];
        }
        closev = sqrt(closev);
        for (c = 0; c < layer1_size; c++)
        	cent[layer1_size * b + c] /= closev;//对质心进行归一化？
      }
      for (c = 0; c < vocab_size; c++)//对所有词语重新分类
      {
        closev = -10;
        closeid = 0;
        for (d = 0; d < clcn; d++)
        {
          x = 0;
          for (b = 0; b < layer1_size; b++)
        	  x += cent[layer1_size * d + b] * syn0[c * layer1_size + b];//内积
          if (x > closev)
          {
            closev = x;
            closeid = d;
          }
        }
        cl[c] = closeid;
      }
    }
    // Save the K-means classes
    for (a = 0; a < vocab_size; a++)
    	fprintf(fo, "%s %d\n", vocab[a].word, cl[a]);
    free(centcn);
    free(cent);
    free(cl);
  }
  fclose(fo);
}

int ArgPos(char *str, int argc, char **argv)
{
  int a;
  for (a = 1; a < argc; a++) 
	  if (!strcmp(str, argv[a]))
		{
			if (a == argc - 1)
			{
				printf("Argument missing for %s\n", str);
				exit(1);
			}
		return a;
		}
  return -1;
}

int main(int argc, char **argv) {
  int i;
  if (argc == 1) {
    printf("WORD VECTOR estimation toolkit v 0.1b\n\n");
    printf("Options:\n");
    printf("Parameters for training:\n");

    //输入文件：已分词的语料
    printf("\t-train <file>\n");
    printf("\t\tUse text data from <file> to train the model\n");

    //输出文件：词向量或者词聚类
    printf("\t-output <file>\n");
    printf("\t\tUse <file> to save the resulting word vectors / word clusters\n");

    //词向量的维度，默认值是100
    printf("\t-size <int>\n");
    printf("\t\tSet size of word vectors; default is 100\n");

    //窗口大小，默认是5
    printf("\t-window <int>\n");
    printf("\t\tSet max skip length between words; default is 5\n");

    //设定词出现频率的阈值，对于常出现的词会被随机下采样
    printf("\t-sample <float>\n");
    printf("\t\tSet threshold for occurrence of words. Those that appear with higher frequency");
    printf(" in the training data will be randomly down-sampled; default is 0 (off), useful value is 1e-5\n");

    //是否采用softmax体系
    printf("\t-hs <int>\n");
    printf("\t\tUse Hierarchical Softmax; default is 1 (0 = not used)\n");

    //负样本的数量，默认是0，通常使用5-10。0表示不使用。
    printf("\t-negative <int>\n");
    printf("\t\tNumber of negative examples; default is 0, common values are 5 - 10 (0 = not used)\n");

    //开启的线程数量
    printf("\t-threads <int>\n");
    printf("\t\tUse <int> threads (default 1)\n");

    //最小阈值。对于出现次数少于该值的词，会被抛弃掉。
    printf("\t-min-count <int>\n");
    printf("\t\tThis will discard words that appear less than <int> times; default is 5\n");

    //学习速率初始值，默认是0.025
    printf("\t-alpha <float>\n");
    printf("\t\tSet the starting learning rate; default is 0.025\n");

    //输出词类别，而不是词向量
    printf("\t-classes <int>\n");
    printf("\t\tOutput word classes rather than word vectors; default number of classes is 0 (vectors are written)\n");

    //debug模式，默认是2，表示在训练过程中会输出更多信息
    printf("\t-debug <int>\n");
    printf("\t\tSet the debug mode (default = 2 = more info during training)\n");

    //是否用binary模式保存数据，默认是0，表示否。
    printf("\t-binary <int>\n");
    printf("\t\tSave the resulting vectors in binary moded; default is 0 (off)\n");

    //保存词汇到这个文件
    printf("\t-save-vocab <file>\n");
    printf("\t\tThe vocabulary will be saved to <file>\n");

    //词汇从该文件读取，而不是由训练数据重组
    printf("\t-read-vocab <file>\n");
    printf("\t\tThe vocabulary will be read from <file>, not constructed from the training data\n");

    //是否采用continuous bag of words算法。默认是0，表示采用另一个叫skip-gram的算法。
    printf("\t-cbow <int>\n");
    printf("\t\tUse the continuous bag of words model; default is 0 (skip-gram model)\n");

    //工具使用样例
    printf("\nExamples:\n");
    printf("./word2vec -train data.txt -output vec.txt -debug 2 -size 200 -window 5 -sample 1e-4 -negative 5 -hs 0 -binary 0 -cbow 1\n\n");
    return 0;
  }
  output_file[0] = 0;
  save_vocab_file[0] = 0;
  read_vocab_file[0] = 0;
  if ((i = ArgPos((char *)"-size", argc, argv)) > 0) layer1_size = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-train", argc, argv)) > 0) strcpy(train_file, argv[i + 1]);
  if ((i = ArgPos((char *)"-save-vocab", argc, argv)) > 0) strcpy(save_vocab_file, argv[i + 1]);
  if ((i = ArgPos((char *)"-read-vocab", argc, argv)) > 0) strcpy(read_vocab_file, argv[i + 1]);
  if ((i = ArgPos((char *)"-debug", argc, argv)) > 0) debug_mode = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-binary", argc, argv)) > 0) binary = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-cbow", argc, argv)) > 0) cbow = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-alpha", argc, argv)) > 0) alpha = atof(argv[i + 1]);
  if ((i = ArgPos((char *)"-output", argc, argv)) > 0) strcpy(output_file, argv[i + 1]);
  if ((i = ArgPos((char *)"-window", argc, argv)) > 0) window = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-sample", argc, argv)) > 0) sample = atof(argv[i + 1]);
  if ((i = ArgPos((char *)"-hs", argc, argv)) > 0) hs = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-negative", argc, argv)) > 0) negative = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-threads", argc, argv)) > 0) num_threads = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-min-count", argc, argv)) > 0) min_count = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-classes", argc, argv)) > 0) classes = atoi(argv[i + 1]);
  vocab = (struct vocab_word *)calloc(vocab_max_size, sizeof(struct vocab_word));
  vocab_hash = (int *)calloc(vocab_hash_size, sizeof(int));
  expTable = (real *)malloc((EXP_TABLE_SIZE + 1) * sizeof(real));
  for (i = 0; i < EXP_TABLE_SIZE; i++)
  {
	//expTable[i] = exp((i -500)/ 500 * 6) 即 e^-6 ~ e^6
    expTable[i] = exp((i / (real)EXP_TABLE_SIZE * 2 - 1) * MAX_EXP); // Precompute the exp() table//计算exp
    //expTable[i] = 1/(1+e^6) ~ 1/(1+e^-6)即 0.01 ~ 1 的样子
    expTable[i] = expTable[i] / (expTable[i] + 1);                   // Precompute f(x) = x / (x + 1)//计算sigmoid
  }
  TrainModel();
  return 0;
}
