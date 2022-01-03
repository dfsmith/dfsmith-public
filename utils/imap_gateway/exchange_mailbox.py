from zope.interface import implementer
from twisted.mail import imap4
from twisted.internet import defer

from exchangelib import Account

import logging
log = logging.getLogger(__name__)

from exchange_credentials import ExchangeAccount

@implementer(imap4.IMailboxInfo, imap4.IMailbox)
class ExchangeMailbox:
    """
    See https://ecederstrand.github.io/exchangelib
    """
    rw = False
    closed = False

    def __init__(self, account: Account, mailbox_name: str, id: int):
        log.info(f"New mailbox: {account} folder={mailbox_name} id={id}")
        self.listeners = []
        self.addListener = self.listeners.append
        self.removeListener = self.listeners.remove
        self.mailbox = account.root / mailbox_name
        self.mailbox.refresh()

    def getHierarchylDelimiter(self):
        return "/"

    def getUnseenCount(self):
        log.info(f"getUnseenCount")
        return self.inbox.unread_count

    def getMessageCount(self):
        log.info(f"getMessageCount")
        return self.inbox.total_count

    def requestStatus(self, names):
        log.info(f"requestStatus {names}")
        r = {}
        if 'MESSAGES' in names:
            r['MESSAGES'] = self.getMessageCount()
        if 'UNSEEN' in names:
            r['UNSEEN'] = self.getUnseenCount()
        return defer.succeed(r)
