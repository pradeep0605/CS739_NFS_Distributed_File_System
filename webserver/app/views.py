from flask import render_template
from app import app

@app.route('/')
@app.route('/index')
def index():
	user = {'nickname': 'I am Foo'}  # fake user
	posts = [  # fake array of posts
		{
			'author': {'nickname': 'John'}, 
			'body': 'Beautiful day in Portland!' 
		},
		{ 
			'author': {'nickname': 'Susan'}, 
			'body': 'The Avengers movie was so cool!' 
		}
	]
	comics = [
		{
			'name': 'Right Mood to do projects',
			'path': '../../static/LastMinutePanic_CalvinHobbes.jpg'
		},
		{
			'name': 'What we do!',
			'path': '../../static/computer_problems.png'
		},
		{
			'name': 'Emacs is the best (So says our prof)',
			'path': '../../static/real_programmers.png'
		},
		{
			'name': 'Wisdom of the ancients',
			'path': '../../static/wisdom_of_the_ancients-1.png'
		},
		{
			'name': 'Study for the exams',
			'path': '../../static/hell.png'
		}

	]
	return render_template("index.html",
			title='Home',
			user=user,
			posts=posts,
			comics=comics)
