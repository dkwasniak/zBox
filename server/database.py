from datetime import datetime
from typing import Optional

from sqlalchemy import create_engine, String, ForeignKey, DateTime
from sqlalchemy.orm import (
    DeclarativeBase,
    Mapped,
    mapped_column,
    relationship,
    sessionmaker,
)


DATABASE_URL = "sqlite:///./data/musicbox.db"

engine = create_engine(DATABASE_URL, connect_args={"check_same_thread": False})
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)


class Base(DeclarativeBase):
    pass


class Track(Base):
    __tablename__ = "tracks"

    id: Mapped[int] = mapped_column(primary_key=True, index=True)
    title: Mapped[str] = mapped_column(String(255), nullable=False)
    filename: Mapped[str] = mapped_column(String(255), nullable=False, unique=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime, default=datetime.utcnow, nullable=False
    )

    figurines: Mapped[list["Figurine"]] = relationship(
        "Figurine", back_populates="track"
    )


class Figurine(Base):
    __tablename__ = "figurines"

    id: Mapped[int] = mapped_column(primary_key=True, index=True)
    name: Mapped[str] = mapped_column(String(255), nullable=False)
    nfc_uid: Mapped[str] = mapped_column(String(64), nullable=False, unique=True)
    track_id: Mapped[Optional[int]] = mapped_column(
        ForeignKey("tracks.id", ondelete="SET NULL"), nullable=True
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime, default=datetime.utcnow, nullable=False
    )

    track: Mapped[Optional["Track"]] = relationship("Track", back_populates="figurines")


def create_tables():
    Base.metadata.create_all(bind=engine)


def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()
