#!/usr/bin/env python3

# See https://github.com/twisted/twisted/blob/imap-howto-536-2/doc/mail/tutorial/imapserver/imapserver.xhtml

from zope.interface.declarations import implementer
from twisted.cred import error
from twisted.cred.portal import Portal
from twisted.cred.portal import IRealm
from twisted.internet import reactor, defer, protocol
from twisted.mail import imap4
from zope.interface import implementer

import logging
logging.basicConfig(
    format="%(levelname)-6s | %(filename)s:%(lineno)d: %(message)s",
    level=logging.NOTSET
)
log = logging.getLogger(__name__)

from exchange_mailbox import ExchangeMailbox
from exchange_credentials import ExchangeAccount, ExchangeCredentialsChecker

class GwAccount(imap4.MemoryAccount):
    mailboxFactory = ExchangeMailbox

    def __init__(self, account: ExchangeAccount):
        log.info(f"account {account}")
        if isinstance(account, error.UnauthorizedLogin):
            imap4.MemoryAccount.__init__(self, account)
        assert(isinstance(account, ExchangeAccount))
        log.info(f"GwAccount instantiated with {account.username}")

        self.acct_info = account
        imap4.MemoryAccount.__init__(self, account.username)
        self.addMailbox("inbox")

    def _emptyMailbox(self, name, id):
        return self.mailboxFactory(self.acct_info.ews_account, name, id)

    def logout(self):
        log.info(f"Logout {self.acct_info.username}")
        return None

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
                return defer.succeed((requestedInterface, agent, agent.logout))
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

    portal = Portal(MailUserRealm())
    portal.registerChecker(ExchangeCredentialsChecker())

    factory = IMAPFactory()
    factory.portal = portal

    reactor.listenTCP(1143, factory)
    log.info("IMAP Server is Listening on TCP 1143...")
    reactor.run()
