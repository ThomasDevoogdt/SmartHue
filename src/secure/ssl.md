# SSL key & cert

# RSA

### command:

```bash
$ openssl req -newkey rsa:2048 -nodes -keyout key.pem -x509 -days 365 -out certificate.pem
```

### expected output:

```bash
$ openssl req -newkey rsa:2048 -nodes -keyout key.pem -x509 -days 365 -out certificate.pem
Generating a 2048 bit RSA private key
..+++
.............................+++
writing new private key to 'key.pem'
-----
You are about to be asked to enter information that will be incorporated
into your certificate request.
What you are about to enter is what is called a Distinguished Name or a DN.
There are quite a few fields but you can leave some blank
For some fields there will be a default value,
If you enter '.', the field will be left blank.
-----
Country Name (2 letter code) [AU]:BE
State or Province Name (full name) [Some-State]:West-Vlaanderen
Locality Name (eg, city) []:Ieper
Organization Name (eg, company) [Internet Widgits Pty Ltd]:Thomas Devoogdt
Organizational Unit Name (eg, section) []:
Common Name (e.g. server FQDN or YOUR name) []:Thomas Devoogdt
Email Address []:thomas.devoogdt@gmail.com
```

## EC


### Generating a private EC key

#### 1. Generate an EC private key, of size 256, and output it to a file named key.pem:

```bash
$ openssl ecparam -name prime256v1 -genkey -noout -out key.pem
```

#### 2. Extract the public key from the key pair, which can be used in a certificate:

```bash
$ openssl ec -in key.pem -pubout -out public.pem
  read EC key
  writing EC key
```

After running these two commands you end up with two files: key.pem and public.pem. These files are referenced in various other guides on this page when dealing with key import.
