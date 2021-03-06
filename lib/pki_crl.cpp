/* vi: set sw=4 ts=4:
 *
 * Copyright (C) 2001 - 2012 Christian Hohnstaedt.
 *
 * All rights reserved.
 */


#include "pki_crl.h"
#include "func.h"
#include "exception.h"
#include "db_base.h"
#include <QDir>

#include "openssl_compat.h"

QPixmap *pki_crl::icon = NULL;

pki_crl::pki_crl(const QString name )
	:pki_x509name(name)
{
	issuer = NULL;
	crl = X509_CRL_new();
	pki_openssl_error();
	pkiType=revocation;
}

void pki_crl::fromPEM_BIO(BIO *bio, QString name)
{
	X509_CRL *_crl;
	_crl = PEM_read_bio_X509_CRL(bio, NULL, NULL, NULL);
	openssl_error(name);
	X509_CRL_free(crl);
	crl = _crl;
	setIntName(rmslashdot(name));
}

QString pki_crl::getMsg(msg_type msg) const
{
	/*
	 * We do not construct english sentences from fragments
	 * to allow proper translations.
	 *
	 * %1 will be replaced by the internal name of the CRL
	 */
	switch (msg) {
	case msg_import: return tr("Successfully imported the revocation list '%1'");
	case msg_delete: return tr("Delete the revocation list '%1'?");
	case msg_create: return tr("Successfully created the revocation list '%1'");
	/* %1: Number of CRLs; %2: list of CRL names */
	case msg_delete_multi: return tr("Delete the %1 revocation lists: %2?");
	}
	return pki_base::getMsg(msg);
}

QSqlError pki_crl::insertSqlData()
{
	XSqlQuery q;
	unsigned name_hash = getSubject().hashNum();

	SQL_PREPARE(q, "SELECT x509super.item FROM x509super "
		"JOIN certs ON certs.item = x509super.item "
		"WHERE x509super.subj_hash=? AND certs.ca=1");
	q.bindValue(0, name_hash);
	q.exec();
	if (q.lastError().isValid())
		return q.lastError();
	while (q.next()) {
		pki_x509 *x = db_base::lookupPki<pki_x509>(q.value(0));
		if (!x) {
			qDebug("CA certificate with id %d not found",
				q.value(0).toInt());
			continue;
		}
		verify(x);
	}
	SQL_PREPARE(q, "INSERT INTO crls (item, hash, num, iss_hash, issuer, crl) "
		  "VALUES (?, ?, ?, ?, ?, ?)");
	q.bindValue(0, sqlItemId);
	q.bindValue(1, hash());
	q.bindValue(2, numRev());
	q.bindValue(3, name_hash);
	q.bindValue(4, issuer ? issuer->getSqlItemId() : QVariant());
	q.bindValue(5, i2d_b64());
	q.exec();
	return q.lastError();
}

void pki_crl::restoreSql(QSqlRecord &rec)
{
	pki_base::restoreSql(rec);
	QByteArray ba = QByteArray::fromBase64(
				rec.value(VIEW_crls_crl).toByteArray());
	d2i(ba);
	setIssuer(db_base::lookupPki<pki_x509>(rec.value(VIEW_crls_issuer)));
}

QSqlError pki_crl::deleteSqlData()
{
	XSqlQuery q;
	QSqlError e;

	SQL_PREPARE(q, "DELETE FROM crls WHERE item=?");
	q.bindValue(0, sqlItemId);
	q.exec();
	return q.lastError();
}

void pki_crl::fload(const QString fname)
{
	FILE *fp = fopen_read(fname);
	X509_CRL *_crl;
	if (fp != NULL) {
		_crl = PEM_read_X509_CRL(fp, NULL, NULL, NULL);
		if (!_crl) {
			pki_ign_openssl_error();
			rewind(fp);
			_crl = d2i_X509_CRL_fp(fp, NULL);
		}
		fclose(fp);
		if (pki_ign_openssl_error()) {
			if (_crl)
				X509_CRL_free(_crl);
			throw errorEx(tr("Unable to load the revocation list in file %1. Tried PEM and DER formatted CRL.").arg(fname));
		}
		X509_CRL_free(crl);
		crl = _crl;
		setIntName(rmslashdot(fname));
		pki_openssl_error();
	} else
		fopen_error(fname);
}

QString pki_crl::getSigAlg() const
{
	return QString(OBJ_nid2ln(X509_CRL_get_signature_nid(crl)));
}

void pki_crl::createCrl(const QString d, pki_x509 *iss )
{
	setIntName(d);
	issuer = iss;
	if (!iss)
		my_error(tr("No issuer given"));
	X509_CRL_set_version(crl, 1); /* version 2 CRL */
	X509_CRL_set_issuer_name(crl, (X509_NAME*)issuer->getSubject().get0());

	pki_openssl_error();
}

a1int pki_crl::getVersion()
{
	return a1int(X509_CRL_get_version(crl));
}

void pki_crl::setLastUpdate(const a1time &a)
{
	a1time t(a);
	X509_CRL_set_lastUpdate(crl, t.get_utc());
	pki_openssl_error();
}

void pki_crl::setNextUpdate(const a1time &a)
{
	a1time t(a);
	X509_CRL_set_nextUpdate(crl, t.get_utc());
	pki_openssl_error();
}

pki_crl::~pki_crl()
{
	X509_CRL_free(crl);
	pki_openssl_error();
}

void pki_crl::d2i(QByteArray &ba)
{
	X509_CRL *c = (X509_CRL*)d2i_bytearray(D2I_VOID(d2i_X509_CRL), ba);
	pki_openssl_error();
	if (c) {
		X509_CRL_free(crl);
		crl = c;
	}
	pki_openssl_error();
}

QByteArray pki_crl::i2d() const
{
	return i2d_bytearray(I2D_VOID(i2d_X509_CRL), crl);
}

void pki_crl::fromData(const unsigned char *p, db_header_t *head)
{
	int size;

	size = head->len - sizeof(db_header_t);

	QByteArray ba((const char*)p, size);
	d2i(ba);

	if (ba.count() > 0) {
		my_error(tr("Wrong Size %1").arg(ba.count()));
	}
}

void pki_crl::addRev(const x509rev &xrev, bool withReason)
{
	X509_CRL_add0_revoked(crl, xrev.get(withReason));
	pki_openssl_error();
}

void pki_crl::addV3ext(const x509v3ext &e)
{
	X509_EXTENSION *ext = e.get();
	X509_CRL_add_ext(crl, ext, -1);
	X509_EXTENSION_free(ext);
	pki_openssl_error();
}

extList pki_crl::extensions() const
{
	extList el;
	el.setStack(X509_CRL_get0_extensions(crl));
	pki_openssl_error();
	return el;
}

bool pki_crl::visible() const
{
	if (pki_x509name::visible())
		return true;
	if (getSigAlg().contains(limitPattern))
		return true;
	return extensions().search(limitPattern);
}

void pki_crl::sign(pki_key *key, const EVP_MD *md)
{
	EVP_PKEY *pkey;
	if (!key || key->isPubKey())
		return;
	X509_CRL_sort(crl);
	pkey = key->decryptKey();
	X509_CRL_sign(crl, pkey, md);
	EVP_PKEY_free(pkey);
	pki_openssl_error();
}

void pki_crl::writeDefault(const QString fname)
{
	writeCrl(fname + QDir::separator() + getUnderlinedName() + ".crl", true);
}

void pki_crl::writeCrl(const QString fname, bool pem)
{
	FILE *fp = fopen_write(fname);
	if (fp != NULL) {
		if (pem)
			PEM_write_X509_CRL(fp, crl);
		else
			i2d_X509_CRL_fp(fp, crl);
		fclose(fp);
		pki_openssl_error();
	} else
		fopen_error(fname);
}

BIO *pki_crl::pem(BIO *b, int format)
{
	(void)format;
	if (!b)
		b = BIO_new(BIO_s_mem());
	PEM_write_bio_X509_CRL(b, crl);
	return b;
}

a1time pki_crl::getLastUpdate() const
{
	return a1time(X509_CRL_get0_lastUpdate(crl));
}

a1time pki_crl::getNextUpdate() const
{
	return a1time(X509_CRL_get0_nextUpdate(crl));
}

int pki_crl::numRev() const
{
	STACK_OF(X509_REVOKED) *st = X509_CRL_get_REVOKED(crl);

	return st ? sk_X509_REVOKED_num(st) : 0;
}

x509revList pki_crl::getRevList()
{
	x509revList ret;
	int i, num = numRev();
	STACK_OF(X509_REVOKED) *st = X509_CRL_get_REVOKED(crl);

	for (i=0; i<num; i++) {
		x509rev r(sk_X509_REVOKED_value(st, i));
		pki_openssl_error();
		ret << r;
	}
	return ret;
}

x509name pki_crl::getSubject() const
{
	return x509name(X509_CRL_get_issuer(crl));
}

bool pki_crl::verify(pki_x509 *issuer)
{
	if (!issuer)
		return false;
	if (getSubject() != issuer->getSubject())
		return false;
	pki_key *key = issuer->getPubKey();
	if (!key)
		return false;
	int ret = X509_CRL_verify(crl, key->getPubKey());
	pki_ign_openssl_error();
	if (ret != 1) {
		delete key;
		return false;
	}
	delete key;
	pki_x509 *curr = getIssuer();
	if (curr && curr->getNotAfter() > issuer->getNotAfter())
		return true;
	setIssuer(issuer);
	return true;
}

void pki_crl::setCrlNumber(a1int num)
{
	ASN1_INTEGER *tmpser = num.get();
	pki_openssl_error();
	X509_CRL_add1_ext_i2d(crl, NID_crl_number, tmpser, 0, 0);
	ASN1_INTEGER_free(tmpser);
	pki_openssl_error();
}

a1int pki_crl::getCrlNumber() const
{
	a1int num;
	if (!getCrlNumber(&num))
		num.set(0L);
	return num;
}

bool pki_crl::getCrlNumber(a1int *num) const
{
	int j;
	ASN1_INTEGER *i;
	i = (ASN1_INTEGER *)X509_CRL_get_ext_d2i(crl, NID_crl_number, &j, NULL);
	pki_openssl_error();
	if (j == -1)
		return false;
	num->set(i);
	ASN1_INTEGER_free(i);
	return true;
}

x509v3ext pki_crl::getExtByNid(int nid)
{
	extList el = extensions();
	x509v3ext e;

	for (int i=0; i< el.count(); i++){
		if (el[i].nid() == nid) return el[i];
	}
	return e;
}

QString pki_crl::printV3ext()
{
	QString text = extensions().getHtml("<br>");
	pki_openssl_error();
	return text;
}

QVariant pki_crl::column_data(dbheader *hd) const
{
	switch (hd->id) {
		case HD_crl_signer:
			if (issuer)
				return QVariant(getIssuerName());
			else
				return QVariant(tr("unknown"));
		case HD_crl_revoked:
			return QVariant(numRev());
		case HD_crl_lastUpdate:
			return QVariant(getLastUpdate().toSortable());
		case HD_crl_nextUpdate:
			return QVariant(getNextUpdate().toSortable());
		case HD_crl_crlnumber:
			a1int a;
			if (getCrlNumber(&a))
				return QVariant(a.toDec());
			return QVariant();
	}
	return pki_x509name::column_data(hd);
}

QVariant pki_crl::getIcon(dbheader *hd) const
{
	return hd->id == HD_internal_name ? QVariant(*icon) : QVariant();
}
