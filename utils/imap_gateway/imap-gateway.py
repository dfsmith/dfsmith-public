#!/usr/bin/env python3

# See https://github.com/twisted/twisted/blob/imap-howto-536-2/doc/mail/tutorial/imapserver/imapserver.xhtml

from exchange_mailbox import ExchangeMailbox
from zope.interface.declarations import implementer
from twisted.cred.checkers import InMemoryUsernamePasswordDatabaseDontUse
from twisted.cred.portal import Portal
from twisted.cred.portal import IRealm
from twisted.internet import reactor, defer, protocol
from twisted.mail import imap4
from zope.interface import implementer

import logging
log = logging.getLogger(__name__)


class GwAccount(imap4.MemoryAccount):
    mailboxFactory = ExchangeMailbox

    def __init__(self, name):
        imap4.MemoryAccount.__init__(self, name)

    def _emptyMailbox(self, name, id):
        return self.mailboxFactory()


@implementer(IRealm)
class MailUserRealm(object):
    agentInterfaces = {
        imap4.IAccount: GwAccount
    }

    def requestAvatar(self, agentId, mind, *interfaces):
        log.debug(f"requestAvatar: {agentId} {mind} {interfaces}")
        for requestedInterface in interfaces:
            if requestedInterface in self.agentInterfaces:
                # return an instance of the correct class
                agent = self.agentInterfaces[requestedInterface](agentId)
                # null logout function: take no arguments and do nothing
                def logout(): return None
                return defer.succeed((requestedInterface, agent, logout))
        # none of the requested interfaces was supported
        raise KeyError("None of the requested interfaces is supported")


class IMAPServerProtocol(imap4.IMAP4Server):
    "Subclass of imap4.IMAP4Server that adds debugging."

    def __init__(self, portal, *args, **kw):
        imap4.IMAP4Server.__init__(self, *args, **kw)
        self.portal = portal
        self.timeoutTest = False

    def lineReceived(self, line):
        log.debug(f"CLIENT <- {line.decode()}")
        imap4.IMAP4Server.lineReceived(self, line)

    def sendLine(self, line):
        imap4.IMAP4Server.sendLine(self, line)
        log.debug(f"SERVER -> {line.decode()}")


class IMAPFactory(protocol.Factory):
    protocol = IMAPServerProtocol
    portal = None  # placeholder

    def buildProtocol(self, address):
        p = self.protocol(self.portal)
        p.factory = self
        return p


if __name__ == "__main__":
    logging.basicConfig(
        format="%(levelname)-6s | %(filename)s:%(lineno)d: %(message)s",
        level=logging.NOTSET
    )

    portal = Portal(MailUserRealm())
    checker = InMemoryUsernamePasswordDatabaseDontUse()
    checker.addUser(b"testuser", b"password")
    portal.registerChecker(checker)

    factory = IMAPFactory()
    factory.portal = portal

    reactor.listenTCP(1143, factory)
    log.info("IMAP Server is Listening on TCP 1143...")
    reactor.run()
