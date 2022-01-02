from zope.interface import implementer
from twisted.mail import imap4
from twisted.internet import defer

from exchangelib import Account

import logging


@implementer(imap4.IMailboxInfo, imap4.IMailbox, imap4.ICloseableMailbox)
class ExchangeMailbox:
    """
    See https://ecederstrand.github.io/exchangelib
    """
    rw = False
    closed = False
    log = logging.getLogger(__name__)
    log.info("loaded info")
    log.warn("loaded warn")

    def __init__(self, account: Account):
        log.warning("init")
        self.listeners = []
        self.addListener = self.listeners.append
        self.removeListener = self.listeners.remove
        account.root.refresh()
        self.inbox = account.inbox

    def getHierarchylDelimiter(self):
        return "/"

    def getUnseenCount(self):
        return self.inbox.unread_count

    def getMessageCount(self):
        return self.inbox.total_count

    def requestStatus(self, names):
        r = {}
        if 'MESSAGES' in names:
            r['MESSAGES'] = self.getMessageCount()
        if 'UNSEEN' in names:
            r['UNSEEN'] = self.getUnseenCount()
        return defer.succeed(r)
