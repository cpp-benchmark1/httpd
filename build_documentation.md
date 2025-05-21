# Documentation Build Guide

> This guide explains how to build the project on **Ubuntu 24.04**. It has been tested inside a virtual machine.

---

## 1 – Install build dependencies

```bash
sudo apt-get -y install \
  libtool-bin subversion build-essential \
  libssl-dev libexpat-dev libpcre3-dev \
  libapr1-dev libaprutil1-dev \
  autoconf libtool libapr1
```

---

## 2 – Check out APR (Apache Portable Runtime)

```bash
svn co http://svn.apache.org/repos/asf/apr/apr/trunk srclib/apr
```

---

## 3 – Normalise line endings 

```bash
find . \
  -type f \
  -not -path '/.git/' \
  -exec dos2unix {} +
```

---

## 4 – Ensure build scripts are executable

```bash
chmod +x buildconf
chmod +x build/get-version.sh
```

---

## 5 – Generate the `configure` script

```bash
./buildconf
```

---

## 6 – Configure the build

```bash
./configure \
  --with-included-apr \
  --enable-mpms-shared=all \
  --with-mpm=event \
  --enable-so \
  --enable-mods-shared=all
```

---

## 7 – Compile

```bash
make -j4   # replace 4 with the number of logical CPU cores available
```

---


