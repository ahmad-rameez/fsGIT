#include "sha1.hpp"
#include <bits/stdc++.h>
#include <fstream>
#include <streambuf>
#include <filesystem> //listing files and sub directory
#include <dirent.h>   // opendir, readdir, closedir
#include <sys/stat.h> // stat
#include <errno.h>    // errno, ENOENT, EEXIST
#if defined(_WIN32)
#include <direct.h>  //  _mkdir
#endif
using namespace std;
namespace fs = std::filesystem;

struct IndexEntry{
    string sha1;
    string path;
};

struct FileTrack{
    vector<string> changed_paths;
    vector<string> new_paths;
    vector<string> deleted_paths;
};

//Object Type Enumeration
enum ObjectType { commit = 1, tree = 2, blob = 3};

//Create the folder and the subfolders
bool isDirExist(const std::string& path){
    #if defined(_WIN32)
        struct _stat info;
        if (_stat(path.c_str(), &info) != 0){
        return false;
        }
        return (info.st_mode & _S_IFDIR) != 0;
    #else 
        struct stat info;
        if (stat(path.c_str(), &info) != 0){
        return false;
        }
        return (info.st_mode & S_IFDIR) != 0;
    #endif
}

bool makePath(const std::string& path){
    #if defined(_WIN32)
        int ret = _mkdir(path.c_str());
    #else
        mode_t mode = 0755;
        int ret = mkdir(path.c_str(), mode);
    #endif
    if (ret == 0)
        return true;
    switch (errno){
    case ENOENT:
        // parent didn't exist, try to create it
        {
            auto pos = path.find_last_of('/');
            if (pos == std::string::npos)
    #if defined(_WIN32)
                pos = path.find_last_of('\\');
            if (pos == std::string::npos)
    #endif
                return false;
            if (!makePath( path.substr(0, pos) ))
                return false;
        }
        // now, try to create again
    #if defined(_WIN32)
        return 0 == _mkdir(path.c_str());
    #else 
        return 0 == mkdir(path.c_str(), mode);
    #endif

    case EEXIST:
        // done!
        return isDirExist(path);

    default:
        return false;
    }
}

string trim(const std::string& str,
                 const std::string& whitespace = " \t")
{
    const auto strBegin = str.find_first_not_of(whitespace);
    if (strBegin == std::string::npos)
        return ""; // no content

    const auto strEnd = str.find_last_not_of(whitespace);
    const auto strRange = strEnd - strBegin + 1;

    return str.substr(strBegin, strRange);
}


string read_file(string path){
    ifstream t(path);
    string str;
    t.seekg(0, ios::end);   
    str.reserve(t.tellg());
    t.seekg(0, ios::beg);
    str.assign((istreambuf_iterator<char>(t)),
            istreambuf_iterator<char>());
    return trim(str);
}

void write_file(string path, string data){
    ofstream out(path,ios::app);
    out << data + '#';
    out.close();
}

void init(){
    string repo;
    cout<<"Enter the name of the repository!"<<endl;
    cin>>repo;
    ofstream out("name.txt");
    out << repo;
    out.close();
    string dir_init[3] = {"git/branches","git/objects","git/refs/heads"};
    for(int i=0;i<3;i++){
        dir_init[i] = repo + '/' + dir_init[i];
        cout<< dir_init[i]<<": "<<(makePath(dir_init[i]) ? "OK" : "failed") << endl;
    }
    ofstream index(repo + "/git/index.txt");
    ofstream HEAD(repo + "/git/HEAD.txt");
    ofstream master(repo + "/git/refs/heads/master.txt");
    cout<<"Initialized Empty Repository Successfully!!!";
    index.close();
    HEAD.close();
    master.close();
}

string get_full_path(string fname){
    string repo_name = read_file("name.txt");
    string file_path = repo_name + '/';
    file_path += fname;
    return file_path;
}


//Hashing Objects in SHA1
string HashObjects(string data, int ObjectType, bool write=true){
    string header = to_string(ObjectType)+to_string(data.length());
    string full_data = header + (string)"00000000" + data;
    SHA1 checksum;
    checksum.update(full_data);
    string sha1 = checksum.final();
    if(write){
        // string dir_init = "repo/git/objects/" + sha1.substr(0,2) + "/" + sha1.substr(2,38);
        string repo = read_file("name.txt");
        string dir_init = repo + '/' +"git/objects/" + sha1.substr(0,2);
        cout<< dir_init <<": "<<(makePath(dir_init) ? "OK" : "failed") << endl;
        ofstream file_object(dir_init + '/' + sha1.substr(2) + ".txt");
        file_object << full_data; //compress if possible, pack then write
        file_object.close();
    }
    return sha1;
}

//Find Object with given SHA1 prefix and return path to object in object store
//or raise error if there is multiple file ,or none!!!
string find_object(string sha1_prefix){
    if(sha1_prefix.length() < 2){
        cerr<<"Dont be lazy!!, atleast type 2 characters!!";
    }
    string repo = read_file("name.txt");
    string obj_dir = repo + '/' + "git/objects/" + sha1_prefix.substr(0,2);
    string rest = sha1_prefix.substr(2);
    const char* PATH = obj_dir.c_str();
    DIR *dir = opendir(PATH);
    struct dirent *entry = readdir(dir);
    int cnt = 0;
    while (entry != NULL){
        if (entry->d_type == DT_DIR && !strcmp(entry->d_name,sha1_prefix.substr(0,2).c_str()))
            cnt++;
        entry = readdir(dir);
    }  
    closedir(dir);
    if(cnt > 1){
        cerr<<"More than one directory!!";
        return obj_dir;
    }
    else if(cnt == 0){
        cerr<<"No Object Found!";
        return NULL;
    }
    else return obj_dir;  
} 

//Read object with given SHA-1 prefix and return pair of
//(object_type, data_bytes), or raise ValueError if not found.
pair<int, string> read_objects(string sha1_prefix){
    string path = find_object(sha1_prefix);
    string full_data = read_file(path);
    int type = (full_data[0] - '0')%48;
    string str1 = "00000000"; 
    size_t found = full_data.find(str1);
    if (found == string::npos)
        cerr << "File is corrupt!!" << endl; 
    string data = full_data.substr(found+8);
    return make_pair(type,data); 
}

//Indexing files to identify files in staging area
//Read git index file and return string of IndexEntry objects.
string read_index(){
    string repo = read_file("name.txt");
    string data = read_file(repo + "/git/index.txt");
    return data;
}

//Write list of IndexEntry objects to git index file.
void write_index(IndexEntry p){
    string packed_entry;
    packed_entry = p.sha1 + "@" + p.path;
    string repo = read_file("name.txt");
    write_file(repo+"/git/index.txt",packed_entry);
}

//Add all file paths to git index.
void add(string path){
    IndexEntry p;
    //string entries = read_index();
    p.sha1 = HashObjects(read_file(path),3);
    cout<<"HASH: "<<p.sha1<<endl;
    p.path = path;
    write_index(p);
}

//Print the list of file in the index (including mode, SHA1, and stage number)
// if details is true
void ls_files(){
    string s = read_index();
    string delimiter = "#";
    size_t pos = 0;
    string token;
    while ((pos = s.find(delimiter)) != std::string::npos) {
        token = s.substr(0, pos);
        cout<<"Path of the file: "<<token.substr(41)<<"\t";
        cout<<"Hash of the file: "<<token.substr(0,40)<<endl;
        s.erase(0, pos + delimiter.length());
    }
}



//Get status of working copy, return tuple of (changed_paths, new_paths,deleted_paths).
FileTrack get_status(){
    string repo = read_file("name.txt");
    string path = repo + "/";
    vector<string> file_path;
    int num_dir_file=0;
    string check = repo + "/git";
    for (const auto & entry : fs::directory_iterator(path)){
        cout << entry.path() << endl;
        if(entry.path() != check){
            file_path.push_back(entry.path());
            num_dir_file++;
        }
    }
    std::sort(file_path.begin(),file_path.end());
    string s = read_index();
    cout<<s<<endl;
    string delimiter = "#",token;
    size_t pos = 0;
    int num_index_files = 0;
    vector<pair<string,string>> index;
    while ((pos = s.find(delimiter)) != std::string::npos) {
        token = s.substr(0, pos);
        index.push_back(make_pair(token.substr(41),token.substr(0,40)));
        s.erase(0, pos + delimiter.length());
        num_index_files++;
    }
    std::sort(index.begin(),index.end());
    FileTrack f;
    for(size_t i=0;i<file_path.size();i++){
        int found = 0;
        for(size_t j=0;j<index.size();j++){
            if(file_path[i]==index[j].first && HashObjects(read_file(file_path[i]),3)!=index[j].second){
                f.changed_paths.push_back(file_path[i]);
                found = 1;
            }
            if(index[j].first>file_path[i])
                break;
        }
        if(!found)
            f.new_paths.push_back(file_path[i]);
    }
    for(size_t i=0;i<index.size();i++){
        int found = 0;
        for(size_t j=0;j<file_path.size();j++){
            if(index[i].first==file_path[j]){
                found = 1;
            }
            // if(index[i].first<file_path[j])
            //     break;
        }
        if(!found)
            f.deleted_paths.push_back(index[i].first);
    }
    return f;
}

void status(){
    FileTrack f = get_status();
    cout<<"Changed Paths are: \n";
    for(size_t i=0;i<f.changed_paths.size();i++)
        cout<<(i+1)<<". "<<f.changed_paths[i]<<endl;
    cout<<"Files newly added: \n";
    for(size_t i=0;i<f.new_paths.size();i++)
        cout<<(i+1)<<". "<<f.new_paths[i]<<endl;
    cout<<"Files deleted: \n";
    for(size_t i=0;i<f.deleted_paths.size();i++)
        cout<<(i+1)<<". "<<f.deleted_paths[i]<<endl;        
}

//driver Code
int main(){  
    cout<<"This is Git implementation by Me!!!!!";
    int ch;
    string path,res,actual_path,fname;
    do{
        cout<<"Choose From the following"<<endl;
        cout<<"1. Initialize a Repository!\n2. Read A File\n3. Add to staging\n4. Ls Files\n5. Status\n";
        cin>>ch;
        switch(ch){
            case 1: init();
                    break;
            case 2: cout<<"Enter the Filename: \n";
                    cin>>fname;
                    cout<<read_file(get_full_path(fname))<<endl;
                    break;
            case 3: cout<<"Enter the filename: \n";
                    cin>>fname;
                    add(get_full_path(fname));
                    break;
            case 4: ls_files();
                    break;
            case 5: status();
                    break;
            case 10: return 0;
                    break;
        }
    }while(ch!=10);
    return 0;
}