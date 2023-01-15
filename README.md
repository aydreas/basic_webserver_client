# Basic HTTP Webserver and CLI Client implementation 

## Build from source
```bash
make
```

## Usage

### Client:
```bash
./client [-p PORT] [ -o FILE | -d DIR ] URL
```
#### Options:
| Option    | Description                                                                                                                              |
|-----------|------------------------------------------------------------------------------------------------------------------------------------------|
| -p [PORT] | Port the client connects to                                                                                                              |
| -o [FILE] | Save response to file [FILE]                                                                                                             |
| -d [DIR]  | Save response in directory [DIR]. Filename is determined by the URL. Example: '/en/about.html' would be saved with filename 'about.html' |
| URL       | URL which should be accessed                                                                                                             |

### Server:
```bash
./server [-p PORT] [-i INDEX] DOC_ROOT
```
#### Options:
| Option    | Description                                               |
|-----------|-----------------------------------------------------------|
| -p [PORT] | Port the server listens to (it always listens to 0.0.0.0) |
| -i [FILE] | File to serve if a directory gets requested               |
| DOC_ROOT  | Root path where all files to be served are stored         |

## License
[MIT](https://choosealicense.com/licenses/mit/)