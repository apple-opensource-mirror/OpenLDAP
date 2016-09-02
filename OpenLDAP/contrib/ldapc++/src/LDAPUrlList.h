/*
 * Copyright 2000, OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */

#ifndef LDAP_URL_LIST_H
#define LDAP_URL_LIST_H

#include <list>
#include <LDAPUrl.h>

typedef std::list<LDAPUrl> UrlList;

/**
 * This container class is used to store multiple LDAPUrl-objects.
 */
class LDAPUrlList{
    public:
	typedef UrlList::const_iterator const_iterator;

        /**
         * Constructs an empty std::list.
         */   
        LDAPUrlList();

        /**
         * Copy-constructor
         */
        LDAPUrlList(const LDAPUrlList& urls);

        /**
         * For internal use only
         *
         * This constructor is used by the library internally to create a
         * std::list of URLs from a array of C-std::strings that was return by
         * the C-API
         */
        LDAPUrlList(char** urls);

        /**
         * Destructor
         */
        ~LDAPUrlList();

        /**
         * @return The number of LDAPUrl-objects that are currently
         * stored in this list.
         */
        size_t size() const;

        /**
         * @return true if there are zero LDAPUrl-objects currently
         * stored in this list.
         */
        bool empty() const;

        /**
         * @return A iterator that points to the first element of the list.
         */
        const_iterator begin() const;
        
        /**
         * @return A iterator that points to the element after the last
         * element of the list.
         */
        const_iterator end() const;

        /**
         * Adds one element to the end of the list.
         * @param attr The attribute to add to the list.
         */
        void add(const LDAPUrl& url);

    private :
        UrlList m_urls;
};
#endif //LDAP_URL_LIST_H
