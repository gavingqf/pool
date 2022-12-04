#pragma once

/*
 * interface of mutex.
 */

class IGNMutex {
public:
	virtual ~IGNMutex() {}
	virtual void lock() = 0;
	virtual void unlock() = 0;
};

class CNonMutex : public IGNMutex {
public:
	CNonMutex() {}
	virtual ~CNonMutex() {}

public:
	virtual void lock() {}
	virtual void unlock() {}
};
